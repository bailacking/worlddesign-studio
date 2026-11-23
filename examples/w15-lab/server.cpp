#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "campaign.hpp"
#include "project_io.hpp"

namespace wds::lab {
namespace {

constexpr qint64 max_line = 16384;
constexpr qint64 max_save = 2 * 1024 * 1024;

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
  Host(Initial initial, QString save, quint16 port, bool read_only)
      : campaign_(std::move(initial.campaign)),
        identity_(std::move(initial.identity)),
        catalog_(std::move(initial.catalog)),
        save_(std::move(save)),
        writer_(save_ + ".lock"),
        read_only_(read_only) {
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
    QObject::connect(&listener_, &QTcpServer::newConnection, &listener_, [this] { accept(); });
    if (!listener_.listen(QHostAddress::LocalHost, port))
      throw std::runtime_error("Unable to listen on loopback");
  }

  quint16 port() const { return listener_.serverPort(); }

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
    while (auto* socket = listener_.nextPendingConnection()) {
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
      if (message.size() != 2) throw std::runtime_error("Invalid hello");
      if (read_only_) {
        send(socket, receipt("ticket_required", campaign_.revision));
        return;
      }
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
  std::map<QTcpSocket*, std::string> peers_;
  QTcpServer listener_;
};

}  // namespace
}  // namespace wds::lab

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  auto args = app.arguments();
  const bool read_only = args.size() > 1 && args.last() == "--read-only";
  if (read_only) args.removeLast();
  if (args.size() != 4 && args.size() != 5) {
    std::cerr << "Usage: wds_w15_server SURFACE_PROJECT CAVERN_PROJECT SAVE_FILE "
                 "[PORT] [--read-only]\n";
    return 2;
  }
  try {
    bool valid = true;
    const auto port = args.size() == 5 ? args[4].toUShort(&valid) : quint16{0};
    if (!valid) throw std::runtime_error("Invalid port");
    wds::lab::Host host(wds::lab::bootstrap(args[1], args[2]), args[3], port, read_only);
    std::cout << wds::lab::Json{{"code", "ready"}, {"port", host.port()}}.dump() << std::endl;
    return app.exec();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
