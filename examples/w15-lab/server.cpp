#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <algorithm>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "campaign.hpp"
#include "project_io.hpp"

namespace wds::lab {
namespace {

constexpr qint64 max_line = 16384;
constexpr qint64 max_save = 2 * 1024 * 1024;

struct Transport {
  QHostAddress bind;
  std::optional<QSslConfiguration> tls{};
  QByteArray invite_digest;
};

QByteArray bounded_file(const QString& path, qint64 limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
    throw std::runtime_error("Unable to read TLS or invitation file");
  const auto bytes = file.read(limit + 1);
  if (bytes.isEmpty() || bytes.size() > limit)
    throw std::runtime_error("Invalid TLS or invitation file size");
  return bytes;
}

bool allowed_address(const QHostAddress& address) {
  if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
  const auto ipv4 = address.toIPv4Address();
  return address.isLoopback() || (ipv4 & 0xff000000U) == 0x0a000000U ||
         (ipv4 & 0xfff00000U) == 0xac100000U || (ipv4 & 0xffff0000U) == 0xc0a80000U;
}

bool valid_invite(const QByteArray& value) {
  return value.size() == 64 && std::ranges::all_of(value, [](char c) {
           return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
         });
}

Transport transport(const QCommandLineParser& args) {
  Transport config{QHostAddress(args.value("bind")), {}, {}};
  if (!allowed_address(config.bind))
    throw std::runtime_error("Use a loopback or private IPv4 address");
  const auto certificate = args.value("tls-cert");
  const auto key = args.value("tls-key");
  const auto invite = args.value("invite-file");
  if (certificate.isEmpty() && key.isEmpty() && invite.isEmpty() && config.bind.isLoopback())
    return config;
  if (certificate.isEmpty() || key.isEmpty() || invite.isEmpty())
    throw std::runtime_error("TLS mode requires a certificate, private key and invitation file");
  if (!QSslSocket::supportsSsl()) throw std::runtime_error("TLS backend unavailable");
  const auto chain = QSslCertificate::fromData(bounded_file(certificate, 64 * 1024));
  const QSslKey private_key(bounded_file(key, 16 * 1024), QSsl::Rsa);
  if (chain.isEmpty() || private_key.isNull())
    throw std::runtime_error("Expected a PEM certificate chain and RSA private key");
  const auto invitation = bounded_file(invite, 128).trimmed();
  if (!valid_invite(invitation))
    throw std::runtime_error("Expected a 64-digit lowercase hex invitation");
  auto tls = QSslConfiguration::defaultConfiguration();
  tls.setProtocol(QSsl::TlsV1_2OrLater);
  tls.setLocalCertificateChain(chain);
  tls.setPrivateKey(private_key);
  tls.setPeerVerifyMode(QSslSocket::VerifyNone);
  config.tls = std::move(tls);
  config.invite_digest = QCryptographicHash::hash(invitation, QCryptographicHash::Sha256);
  return config;
}

bool matches_invite(const QByteArray& expected, const std::string& text) {
  const auto candidate = QByteArray::fromStdString(text);
  if (!valid_invite(candidate)) return false;
  const auto digest = QCryptographicHash::hash(candidate, QCryptographicHash::Sha256);
  unsigned mismatch = 0;
  for (qsizetype index = 0; index < digest.size(); ++index)
    mismatch |= static_cast<unsigned char>(digest.at(index) ^ expected.at(index));
  return mismatch == 0;
}

struct Initial {
  Campaign campaign;
  Json identity = Json::object();
  Json catalog;
};

Initial bootstrap(const QString& surface, const QString& cavern) {
  Initial result;
  const std::array paths{surface, cavern};
  for (std::size_t i = 0; i < paths.size(); ++i) {
    const auto loaded =
        platform::start_scene(std::filesystem::path(paths[i].toStdU16String()), 0, true);
    if (!loaded.voxel_scene || !loaded.voxel_scene->workshop)
      throw std::runtime_error("Both revision-zero projects must have a voxel workshop");
    const auto& scene = *loaded.voxel_scene;
    auto json = platform::detail::parse_json(scene.source.before);
    auto catalog = json.at("workshop");
    if (catalog.contains("progress"))
      throw std::runtime_error("Use source projects with empty inventory");
    if (i == 0)
      result.catalog = catalog;
    else if (result.catalog != catalog)
      throw std::runtime_error("Both worlds must share the same item catalog");
    std::ostringstream material;
    voxel::write_content(material, scene.world.content());
    const auto source = scene.source.project_id + "\n" + scene.source.before + material.str();
    const auto hash =
        QCryptographicHash::hash(QByteArray::fromStdString(source), QCryptographicHash::Sha256)
            .toHex();
    result.identity[world_names[i]] = hash.toStdString();
    result.campaign.worlds[i] = scene.world;
  }
  return result;
}

Json receipt(std::string_view code, std::uint64_t revision) {
  return {{"v", 1}, {"type", "receipt"}, {"code", code}, {"revision", revision}};
}

class Host {
 public:
  Host(Initial initial, QString save, quint16 port, bool read_only, Transport transport)
      : campaign_(std::move(initial.campaign)),
        identity_(std::move(initial.identity)),
        catalog_(std::move(initial.catalog)),
        save_(std::move(save)),
        writer_(save_ + ".lock"),
        read_only_(read_only),
        invite_digest_(std::move(transport.invite_digest)) {
    writer_.setStaleLockTime(0);
    if (!writer_.tryLock()) throw std::runtime_error("Campaign already has a writer");
    const bool exists = QFileInfo::exists(save_);
    if (read_only_ && !exists) throw std::runtime_error("Read-only mode requires an existing save");
    if (exists) {
      QFile file(save_);
      if (!file.open(QIODevice::ReadOnly) || file.size() > max_save)
        throw std::runtime_error("Unable to load campaign");
      campaign_ = load_campaign(platform::detail::parse_json(file.readAll().toStdString()),
                                std::move(campaign_), identity_, catalog_);
    }
    if (transport.tls) {
      auto secure = std::make_unique<QSslServer>();
      secure->setSslConfiguration(*transport.tls);
      listener_ = std::move(secure);
    } else {
      listener_ = std::make_unique<QTcpServer>();
    }
    listener_->setMaxPendingConnections(static_cast<int>(max_players * 2));
    QObject::connect(listener_.get(), &QTcpServer::pendingConnectionAvailable, listener_.get(),
                     [this] { accept(); });
    if (!listener_->listen(transport.bind, port)) throw std::runtime_error("Unable to bind server");
  }

  quint16 port() const { return listener_->serverPort(); }

 private:
  bool commit(Campaign next) {
    if (campaign_.revision == max_revision) return false;
    next.revision = campaign_.revision + 1;
    const auto bytes = save_campaign(next, identity_).dump() + "\n";
    if (bytes.size() > static_cast<std::size_t>(max_save)) return false;
    QSaveFile file(save_);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(bytes.data(), static_cast<qint64>(bytes.size())) !=
            static_cast<qint64>(bytes.size()) ||
        !file.commit())
      return false;
    campaign_ = std::move(next);
    return true;
  }

  void accept() {
    while (auto* socket = listener_->nextPendingConnection()) {
      if (peers_.size() >= max_players * 2) {
        socket->abort();
        socket->deleteLater();
        continue;
      }
      socket->setReadBufferSize(max_line + 1);
      peers_.emplace(socket, "");
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { read(socket); });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] {
        peers_.erase(socket);
        socket->deleteLater();
      });
      if (socket->bytesAvailable()) read(socket);
    }
  }

  static void send(QTcpSocket* socket, const Json& object) {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    const auto bytes = object.dump() + "\n";
    constexpr qint64 max_queue = 2 * max_save;
    if (socket->bytesToWrite() + static_cast<qint64>(bytes.size()) > max_queue) {
      socket->abort();
      return;
    }
    socket->write(bytes.data(), static_cast<qint64>(bytes.size()));
  }

  void broadcast() {
    const auto peers = peers_;
    for (const auto& [socket, ticket] : peers)
      if (!ticket.empty()) send(socket, snapshot(campaign_, campaign_.players.at(ticket)));
  }

  void read(QTcpSocket* socket) {
    while (socket->canReadLine()) {
      const auto bytes = socket->readLine(max_line + 2);
      if (bytes.size() > max_line) {
        socket->abort();
        return;
      }
      try {
        const auto message = platform::detail::parse_json(bytes.toStdString());
        handle(socket, message);
      } catch (const std::exception&) {
        send(socket, receipt("invalid_message", campaign_.revision));
      }
      if (!peers_.contains(socket)) return;
    }
    if (socket->bytesAvailable() >= max_line) socket->abort();
  }

  void join(QTcpSocket* socket, const Json& message) {
    std::string ticket;
    if (message.contains("ticket")) {
      if (message.size() != 3 || !message["ticket"].is_string())
        throw std::runtime_error("Invalid hello");
      ticket = message["ticket"].get<std::string>();
      if (!campaign_.players.contains(ticket)) {
        send(socket, receipt("unknown_ticket", campaign_.revision));
        return;
      }
    } else {
      const bool has_invite = message.contains("invite");
      if (message.size() != 2U + static_cast<std::size_t>(has_invite) ||
          (has_invite && !message["invite"].is_string()))
        throw std::runtime_error("Invalid hello");
      if (read_only_) {
        send(socket, receipt("ticket_required", campaign_.revision));
        return;
      }
      if (!invite_digest_.isEmpty() &&
          (!has_invite ||
           !matches_invite(invite_digest_, message["invite"].get_ref<const std::string&>()))) {
        send(socket, receipt("invite_required", campaign_.revision));
        return;
      }
      if (invite_digest_.isEmpty() && has_invite) throw std::runtime_error("Invalid hello");
      if (campaign_.players.size() >= max_players) {
        send(socket, receipt("player_limit", campaign_.revision));
        return;
      }
      ticket = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
      auto inventory = items::read_workshop(catalog_, true);
      auto next = campaign_;
      next.players.emplace(ticket, Explorer{"player-" + std::to_string(next.players.size() + 1),
                                            0,
                                            {},
                                            std::move(*inventory.workshop)});
      if (!commit(std::move(next))) {
        send(socket, receipt("save_failed", campaign_.revision));
        return;
      }
    }
    const auto old =
        std::ranges::find_if(peers_, [&](const auto& peer) { return peer.second == ticket; });
    if (old != peers_.end()) {
      auto* previous = old->first;
      old->second.clear();
      previous->abort();
    }
    peers_.at(socket) = ticket;
    send(socket, {{"v", 1}, {"type", "welcome"}, {"ticket", ticket}});
    broadcast();
  }

  void handle(QTcpSocket* socket, const Json& message) {
    if (!message.is_object() || checked_number(message.at("v"), 1) != 1 ||
        !message.at("type").is_string())
      throw std::runtime_error("Invalid frame");
    const auto& type = message["type"].get_ref<const std::string&>();
    const auto ticket = peers_.at(socket);
    if (ticket.empty()) {
      if (type == "hello")
        join(socket, message);
      else
        send(socket, receipt("hello_required", campaign_.revision));
      return;
    }
    const auto& current = campaign_.players.at(ticket);
    if (type == "snapshot" && message.size() == 2) {
      send(socket, snapshot(campaign_, current));
      return;
    }
    if (type != "action" || message.size() != 5) throw std::runtime_error("Invalid action");
    if (read_only_) {
      send(socket, receipt("permission_denied", campaign_.revision));
      return;
    }
    const auto revision = checked_number(message.at("revision"));
    const auto sequence = checked_number(message.at("seq"));
    const auto& command = message.at("command");
    if (sequence == current.sequence && sequence != 0) {
      send(socket, receipt(command == current.last_command ? "replayed" : "sequence_conflict",
                           campaign_.revision));
      return;
    }
    if (sequence != current.sequence + 1) {
      send(socket, receipt("invalid_sequence", campaign_.revision));
      return;
    }
    if (revision != campaign_.revision) {
      send(socket, receipt("revision_conflict", campaign_.revision));
      return;
    }
    auto next = campaign_;
    auto& explorer = next.players.at(ticket);
    const auto code = apply(next, explorer, command);
    if (code != "ok") {
      send(socket, receipt(code, campaign_.revision));
      return;
    }
    explorer.sequence = sequence;
    explorer.last_command = command;
    const bool committed = commit(std::move(next));
    send(socket, receipt(committed ? "ok" : "save_failed", campaign_.revision));
    if (committed) broadcast();
  }

  Campaign campaign_;
  Json identity_;
  Json catalog_;
  QString save_;
  QLockFile writer_;
  bool read_only_;
  QByteArray invite_digest_;
  std::map<QTcpSocket*, std::string> peers_;
  std::unique_ptr<QTcpServer> listener_;
};

}  // namespace
}  // namespace wds::lab

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addPositionalArgument("surface", "Surface project");
  parser.addPositionalArgument("cavern", "Cavern project");
  parser.addPositionalArgument("save", "Campaign save");
  parser.addPositionalArgument("port", "Server port, defaults to an available port", "[port]");
  parser.addOption({"read-only", "Allow ticket holders to read but not modify the campaign"});
  parser.addOption({"bind", "Local interface address", "IPv4", "127.0.0.1"});
  parser.addOption({"tls-cert", "Server certificate chain in PEM format", "FILE"});
  parser.addOption({"tls-key", "Server RSA private key in PEM format", "FILE"});
  parser.addOption({"invite-file", "Invitation for new players", "FILE"});
  parser.process(app);
  const auto args = parser.positionalArguments();
  if (args.size() != 3 && args.size() != 4) parser.showHelp(2);
  try {
    bool valid = true;
    const auto port = args.size() == 4 ? args[3].toUShort(&valid) : quint16{0};
    if (!valid) throw std::runtime_error("Invalid port");
    auto transport = wds::lab::transport(parser);
    const bool encrypted = transport.tls.has_value();
    wds::lab::Host host(wds::lab::bootstrap(args[0], args[1]), args[2], port,
                        parser.isSet("read-only"), std::move(transport));
    std::cout << wds::lab::Json{{"code", "ready"}, {"port", host.port()}, {"tls", encrypted}}.dump()
              << std::endl;
    return app.exec();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
