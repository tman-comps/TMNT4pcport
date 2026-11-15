# Code server for online play

With a code server, online play needs no router setup from the players: the host picks *Host with code* and gets a four-character code, the other player enters it under *Join with code*, and the server passes the game's packets between the two.

`tmnt_relay.py` is the whole server: one Python 3 file, no dependencies, nothing written to disk. It only relays between players who paired with a code, at a capped rate, and forgets a game a minute after its last packet. Traffic is tiny (about 60 packets of under 100 bytes a second per player). Running it at one of the players' homes costs nothing in lag: for them the relay is on the local network, for the other player it's the same distance as a direct connection.

## Running it

**Synology (Container Manager):**

1. Copy `tmnt_relay.py` and `compose.yaml` into a folder on the NAS, e.g. `docker/tmnt-relay`.
2. Container Manager → *Project* → *Create*: name `tmnt-relay`, path = that folder, source = the existing `compose.yaml`. Build/start it.
3. If the Synology firewall is on: allow UDP port 7846.

**Anywhere else:** `docker compose up -d` in this folder, or just `python3 tmnt_relay.py --port 7846`.

## Making it reachable

The server needs **UDP** port 7846 forwarded from the router to the machine it runs on (on a FRITZ!Box: *Internet → Permit Access → Port Sharing*, add the NAS, a port sharing for UDP 7846). A reverse proxy for websites doesn't help here: those pass HTTP over TCP, and the game uses UDP. The connection needs a public IPv4 address (not DS-Lite / carrier-grade NAT). A dynamic DNS name for that address (MyFRITZ!, Synology DDNS, your own domain) saves typing.

## Pointing the game at it

In `tmnt.ini` next to the program, add a line (both players):

```
online_server=yourname.example.com:7846
```

or start the game with `--server yourname.example.com:7846`. The *Online play* menu then shows *Host with code*, *Code* and *Join with code*. From the command line: `--host-code`, or `--code K7QX` to join.

If you play from inside the network the server runs in and your router doesn't route its own public address back in, use the NAS's local address there (e.g. `online_server=192.168.178.20:7846`).
