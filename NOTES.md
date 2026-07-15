# Notes

My design uses a mix of forward error correction and NACKs to handle dropped packets without blowing past the 2x bandwidth limit. 
The sender uses a basic token bucket idea: it gets 315 "tokens" every frame, and it spends them to attach a second, older frame to its outgoing packets whenever it can (which ends up being like 97% of the time).
When picking which old frame to attach, it first checks if the receiver sent a NACK asking for a specific frame. 
If nobody is asking for anything, it just defaults to attaching the frame from 3 steps ago to deal with random short burst losses.
The receiver doesn't even need a complicated timer loop; it just grabs incoming packets, unpacks them, and immediately hands them to the harness player since the player handles early arrivals fine.
It also keeps track of the highest sequence it has seen, and if it notices it skipped a number recently, it fires off a NACK back to the sender.
For the grading profiles, I'd suggest grading at a delay of **180 ms**. 
This delay handles pretty much all normal packet drops and minor lag spikes.
What breaks it is if the network just totally dies for a long stretch, or if a massive lag spike causes the NACKs to take longer than 180ms to make the round trip.
