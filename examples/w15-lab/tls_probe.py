"""Exercise TLS invitation, cross-world synchronization and restart on loopback."""

import argparse
import json
import os
from pathlib import Path
import secrets
import socket
import ssl
import subprocess
import tempfile

from probe import Client, encoded, server, templates


def credentials(folder):
    certificate = folder / "server.pem"
    key = folder / "server-key.pem"
    if os.name == "nt":
        environment = {**os.environ, "WDS_TEST_CERT": str(certificate), "WDS_TEST_KEY": str(key)}
        subprocess.run(
            ["pwsh", "-NoProfile", "-NonInteractive", "-Command", r"""
$ErrorActionPreference = 'Stop'
$rsa = [System.Security.Cryptography.RSA]::Create(2048)
$request = [System.Security.Cryptography.X509Certificates.CertificateRequest]::new(
  'CN=127.0.0.1', $rsa, [System.Security.Cryptography.HashAlgorithmName]::SHA256,
  [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
$san = [System.Security.Cryptography.X509Certificates.SubjectAlternativeNameBuilder]::new()
$san.AddIpAddress([System.Net.IPAddress]::Loopback)
$request.CertificateExtensions.Add($san.Build())
$cert = $request.CreateSelfSigned(
  [DateTimeOffset]::UtcNow.AddMinutes(-5), [DateTimeOffset]::UtcNow.AddDays(1))
[IO.File]::WriteAllText($env:WDS_TEST_CERT, $cert.ExportCertificatePem())
[IO.File]::WriteAllText($env:WDS_TEST_KEY, $rsa.ExportPkcs8PrivateKeyPem())
"""], env=environment, check=True, timeout=30, capture_output=True,
        )
    else:
        config = folder / "openssl.cnf"
        config.write_text(
            "[req]\nprompt = no\ndistinguished_name = dn\nx509_extensions = extensions\n"
            "[dn]\nCN = 127.0.0.1\n[extensions]\nsubjectAltName = IP:127.0.0.1\n",
            encoding="ascii",
        )
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
             "-config", config, "-keyout", key, "-out", certificate],
            check=True, timeout=30, capture_output=True,
        )
    return certificate, key


def denied(port, context, code, **fields):
    with socket.create_connection(("127.0.0.1", port), timeout=4) as connection:
        with context.wrap_socket(connection, server_hostname="127.0.0.1") as secure:
            secure.sendall(encoded({"v": 1, "type": "hello", **fields}))
            with secure.makefile("rb") as response:
                assert json.loads(response.readline())["code"] == code


def verify(exe, folder):
    surface, cavern = templates(folder)
    save = folder / "campaign.json"
    cert, key = credentials(folder)
    invitation = folder / "invitation.txt"
    token = secrets.token_hex(32)
    invitation.write_text(token + "\n", encoding="ascii")
    flags = ("--tls-cert", cert, "--tls-key", key, "--invite-file", invitation)
    context = ssl.create_default_context(cafile=str(cert))
    context.minimum_version = ssl.TLSVersion.TLSv1_2

    for bind in ("0.0.0.0", "192.168.1.2"):
        result = subprocess.run(
            [exe, surface, cavern, save, "--bind", bind], timeout=8, capture_output=True,
        )
        assert result.returncode != 0 and not result.stdout and not save.exists(), result

    with server(exe, surface, cavern, save, *flags) as port:
        with socket.create_connection(("127.0.0.1", port), timeout=4) as unexpected:
            try:
                ssl.create_default_context().wrap_socket(unexpected, server_hostname="127.0.0.1")
            except ssl.SSLCertVerificationError:
                pass
            else:
                raise AssertionError("The test certificate must be trusted explicitly")
        denied(port, context, "invite_required")
        denied(port, context, "invite_required", invite="0" * 64)
        assert not save.exists()
        with Client(port, context=context, invite=token) as a:
            with Client(port, context=context, invite=token) as b:
                assert a.connection.version() in ("TLSv1.2", "TLSv1.3")
                assert b.connection.version() in ("TLSv1.2", "TLSv1.3")
                a.observe(b.latest["revision"])
                shared = b.observe(a.action("travel", world="cavern"))
                assert any(p["id"] == a.latest["you"] and p["world"] == "cavern"
                           for p in shared["players"])
                last_revision = shared["revision"]
                ticket = a.ticket
        assert save.exists() and token.encode("ascii") not in save.read_bytes()

    before = save.read_bytes()
    with server(exe, surface, cavern, save, *flags, "--read-only") as port:
        denied(port, context, "ticket_required", invite=token)
        with Client(port, ticket, context=context) as client:
            state = client.query()
            assert state["revision"] == last_revision and state["world"] == "cavern"
            client.action("move", direction="east", expected="permission_denied")
            assert client.query() == state
    assert save.read_bytes() == before
    print("TLS invitation, two-client world sync and ticket-only read-only restart passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    root = args.work.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="wds-tls-", dir=root) as scratch:
        verify(args.server.resolve(strict=True), Path(scratch))
