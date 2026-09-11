# Morse protocol

Protocol for sending small messages between heltec v3 or similar devices

## IDs

every device has a 4 bit ID.
The ID 0 acts as a wildcard.
This allows up to 15 unique devices.

## Messages

Each message has a specific 4 bit ID, telling what kind of message it is:

- 0000 : Ping
- 0001 : Pong
- 0010 : Message

### Ping

Any device can ping any other device or all using the wilcard id. A ping message looks like the following, with the first 4 bits denoting that it is a ping, and the last 4 bits the device it wants to ping.

0000 XXXX
Ping  ID

