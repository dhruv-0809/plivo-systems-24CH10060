# Flaky Network

My setup for the flaky network problem. It just sends frames over UDP and tries to survive packet drops and latency spikes using a mix of FEC and basic NACKs. Written in C++.

## building and running

```bash
make
```

to run it against a profile (e.g. profile B with a 180ms delay):
```bash
python3 run.py --profile profiles/B.json --delay_ms 180
```

## how it works

- **sender**: I put a strict token bucket on the sender to make sure it doesn't accidentally exceed the 2x bandwidth cap. it gets 315 tokens per frame. whenever it has enough tokens, it attaches an older frame to the current packet. it prioritizes frames that the receiver sent a NACK for. if there are no NACKs, it just attaches the frame from 3 steps ago by default to cover random drops.
- **receiver**: super simple. it just unpacks everything that comes in and forwards it straight to the player right away. it also tracks the highest sequence number seen, and if it notices a gap, it sends a quick NACK packet back to the sender.

## files
- `sender.cpp`: sender logic and token bucket
- `receiver.cpp`: gap detection and forwarding
- `Makefile`: build script
