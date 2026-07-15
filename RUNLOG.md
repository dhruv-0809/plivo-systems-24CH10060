# RUNLOG

| Profile | delay_ms | miss % | overhead | What I changed and why |
|---------|----------|--------|----------|------------------------|
| A       | 100      | 0.73%  | 1.97x    | Wrote the first version combining basic error correction with NACKs. Set the extra frame offset to 3, and put a hard cap on bandwidth using a token bucket. Passed fine! |
| B       | 140      | 1.47%  | 1.97x    | Kept the exact same code but ran on Profile B. Failed the miss cap. The problem is that the delay is too tight for NACKs to make it back and forth in time when latency spikes. |
| B       | 250      | 0.33%  | 1.97x    | Bumped the delay way up to 250ms just to see if the logic works when given enough time. It passed easily. |
| B       | 200      | 0.47%  | 1.97x    | Lowered the delay to 200ms to get a better score. Still comfortably under the 1% miss limit. |
| B       | 180      | 0.40%  | 1.97x    | Pushed it down to 180ms. It still passes! The combination of just repeating the frame from 3 steps ago plus sending NACKs for whatever we missed is fast enough. I'll stick with 180ms for Profile B. |
