# Blacksmith Master Server

This folder contains a small web front end and HTTP server for playing against
the same Master model used by `play.cmd`. The server imports the existing
Python `master` package and native `blacksmith.master` rules binding. It does
not copy the model or maintain a second rules implementation.

## Start on Windows

Build the project once, then launch:

```powershell
server\start.cmd
```

The server uses `models/blacksmith-master/master-0099.pt` for every web match.
The page prints the local URL and the addresses it found for other devices. By default
it listens on `0.0.0.0:8765`; open the printed `http://<computer-ip>:8765/` URL
on another device on the same network. If Windows Firewall asks, allow inbound
TCP port 8765 on the Private profile. If it does not prompt, open PowerShell
as Administrator and add a rule scoped to the local subnet:

```powershell
.\server\allow-firewall.ps1
```

For a different server port, pass the same port number, for example
`.\server\allow-firewall.ps1 -Port 9000`.

From another Windows device, check the route before opening the page:

```powershell
Test-NetConnection <本机IP> -Port 8765
Invoke-RestMethod http://<本机IP>:8765/api/config
```

The first command should report `TcpTestSucceeded: True`; the second should
report that the engine is ready.

The same play options are available on the command line:

```powershell
server\start.cmd --starting-iron 0.5 --search-depth 3 --width 24 --leaf-width 4
server\start.cmd --host 127.0.0.1 --port 9000
```

For direct command-line use, `--checkpoint` can override the default model.
`--checkpoint-dir` can explicitly restore random selection from a checkpoint
directory.

## Start on Linux

After `bash build.sh`, start the same server with:

```bash
bash server/start.sh
```

It also listens on `0.0.0.0:8765` by default. If UFW is enabled, allow the
server port from the trusted client subnet:

```bash
sudo ufw allow from <trusted-client-subnet> to any port 8765 proto tcp
```

From an Ubuntu client, check the server before opening its page:

```bash
curl -v --connect-timeout 5 http://<server-ip>:8765/api/config
```

Use the `http://<server-ip>:8765/` address in a browser once the engine responds.

`--host 127.0.0.1` makes the server local-only. Omit `--host` to accept
connections on the computer's network interfaces. The server has no login or
TLS layer; use it on a trusted network and do not forward its port to the
public internet.

## GitHub Pages with an ngrok API tunnel

The static browser client can be published from `server/web` with the workflow
in `.github/workflows/pages.yml`, which runs on pushes to `main` or
`blacksmith-master`. In the repository settings, set **Pages →
Build and deployment → Source** to **GitHub Actions**. After the workflow runs,
the page is available at `https://neonnicknick.github.io/blacksmith.cpp/`.

`server/web/config.js` points the browser at the configured ngrok URL. Keep
the server and ngrok tunnel running while playing. If the tunnel URL changes,
update that file and publish the change. The server allows browser API requests
from `https://neonnicknick.github.io` by default; use `--cors-origin` if the
Pages domain changes. Restart the server after updating CORS settings.

The public tunnel exposes the game API to anyone who can reach its URL. The
server has no authentication, so stop the tunnel when it is not needed.

## Controls

- Choose 0, 0.5, or 1 starting iron. Master always starts at 0 iron.
- Enter a skill and parameter in the same format as local `play.cmd`; the
  autocomplete list contains only actions currently legal for the player.
- The Master chooses its simultaneous move before the player submits theirs.
- The page shows HP, role, resources, and the model value estimate. That value
  is expected score (win 1, draw 0.5, loss 0), not a calibrated win chance.
- After a game ends, review round positions, inspect both sides' search
  recommendations, or trial a position. Trial moves do not change the record.

Game records and sessions live in server memory and are cleared when the
server stops.
