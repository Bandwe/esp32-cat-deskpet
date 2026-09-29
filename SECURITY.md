# Security and privacy

- Never commit `.env`, Wi-Fi passwords, pairing tokens, session secrets,
  passwords or password hashes from a real deployment.
- Website application source and generic deployment examples may be public;
  real server infrastructure and access details must remain private. Never
  publish production domains/IPs, SSH access settings, panel URLs, account
  details, private keys, actual deployment paths or operational scripts.
  Keep examples on reserved domains and loopback addresses, not real servers.
  Ignore rules are an extra safeguard, not a substitute for checking staged
  files and history before pushing.
- An ESP32 flash/NVS backup may include credentials even when it looks like
  firmware. Do not upload flash dumps, factory backups or device logs.
- Use a unique strong website password. Serve the website through HTTPS with
  secure session cookies, and keep the database and `.env` outside public web
  roots. The server README describes local versus production settings.
- The firmware's TLS client trusts ISRG Root X1. A different certificate chain
  needs a matching trusted root in the firmware; do not disable verification.
- Pairing tokens authorize a device. Re-pair/revoke access after loss or
  disclosure. Do not paste tokens, pairing codes or diagnostic logs containing
  them in public issues.
- Wi-Fi and device settings are kept in ESP32 NVS; this project does not enable
  flash encryption or secure boot. It is a personal prototype, not a hardened
  multi-tenant service.
- The public PC bridge requires selecting a device and checking its firmware
  identity before sending telemetry. Do not remove those checks to resolve a
  connection failure.

For security reports, do not include live credentials in a public GitHub issue.
Use GitHub's private vulnerability reporting if enabled, or request a private
contact channel without publishing the sensitive material.
