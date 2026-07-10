# MESH 101 — learn mesh networking with two badges

You need: two badges running this launcher, antennas attached, and a friend
(or your other hand). Total time: about fifteen minutes, and by the end you'll
have run the same radio hardware on two *different* mesh networks.

## What is a mesh, in one paragraph

There's no tower and no router. Every badge is both a phone *and* a relay:
when you send a message, nearby badges hear it and repeat it outward, hop by
hop, until it reaches the whole network. Nodes can join, leave, or wander —
the mesh heals around them. That's it. Everything else is details: how nodes
introduce themselves (adverts), how messages avoid looping forever (hop
limits), and how strangers' messages stay private (encryption).

## Exercise 1 — first contact (MeshCore messenger)

Both badges boot the **meshcore-messenger** out of the box.

1. Walk through the touch wizard on each badge. Name one badge after
   yourself; name the other after your friend.
2. On either badge, tap **Advert** — that broadcasts "I exist, here's my
   public key" once. Watch the other badge: your friend appears in Contacts.
3. Open the contact, type a message (yes, there's a full on-screen
   keyboard), send. The ✓ that appears is an *acknowledgement* — the other
   radio actually confirmed receipt. That round trip just happened over
   LoRa: no WiFi, no cell service, no infrastructure.
4. Now look at the message details: **RSSI** (signal strength, more negative
   = weaker) and **SNR** (signal vs noise). Put a building between you and
   watch the numbers change. LoRa trades speed for range — these slow, tough
   packets can cross kilometers.

**Idea to hold onto:** contacts are cryptographic identities (key pairs),
not phone numbers. Messages to you are encrypted so only you can read them —
relays repeat what they cannot read.

## Exercise 2 — same radio, different mesh (Meshtastic)

Now the fun part. Hold **UP**, tap **RESET** (keep holding UP ~1 s) — the
launcher menu appears. Install **meshtastic-mui** on both badges.

1. First boot asks for your **region** — pick US (this is a legal thing:
   different countries allocate different radio bands. You just learned
   about ISM bands the practical way).
2. Give each badge an owner name in settings.
3. Both badges land on **LongFast** — Meshtastic's default public channel.
   Send a message: it reaches every LongFast node in radio range, not just
   your friend. Different philosophy: Meshtastic starts *public*
   (channel = shared group key), MeshCore starts *private* (per-contact
   encryption). Neither is wrong; they're different answers to "who should
   hear me by default?"
4. If other Meshtastic nodes live in your area, they may already be showing
   up in your node list. You joined a mesh that was always there — you just
   never had a radio for it.

## Exercise 3 — the part that should surprise you

Switch both badges back to **meshcore-messenger** via the launcher.

Your contacts and chat history are still there. Switch to Meshtastic again —
your owner name and region are still there too. Each firmware owns a private
slice of the badge's flash, so "changing operating systems" never costs you
an identity. Swap all day.

## Where to go from here

- **Repeaters & room servers:** the launcher card ships companion/repeater
  builds of MeshCore on the DefconBadge2026 releases page — a third badge on
  a hill becomes everyone's range extender, and a room server is a tiny BBS
  your friends' badges can post to.
- **Phone apps:** meshtastic-standard pairs with the Meshtastic phone app
  over Bluetooth; the MeshCore builds pair with the MeshCore app.
- **Range games:** how far can two badges reach? Parking garage vs park?
  Watch SNR fall as distance grows, then move three meters and watch it
  come back. Radio is spooky like that.
- **Build your own guest:** any app-format .bin built against the launcher's
  partition table goes in /firmware and shows up in the menu. The
  badge-launcher repo README documents the etiquette (short version: fit
  the slot, keep your hands off partitions that aren't yours).

Welcome to the mesh. 📡
