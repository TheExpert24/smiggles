#!/usr/bin/env python3
import sys
import struct
import math
from pathlib import Path

def main(argv):
    if len(argv) < 3:
        return 1
    infile = Path(argv[1])
    outfile = Path(argv[2])
    
    if not infile.exists():
        return 1
        
    import wave
    with wave.open(str(infile), 'rb') as w:
        params = w.getparams()
        frames = w.readframes(params.nframes)
        
    sample_rate = params.framerate
    channels = params.nchannels
    width = params.sampwidth
    
    step = sample_rate // 10
    if step <= 0: step = 1
    
    out_notes = []
    
    for i in range(0, params.nframes, step):
        idx = i * channels * width
        if idx >= len(frames) - 2: break
        
        if width == 2:
            val = struct.unpack_from('<h', frames, idx)[0]
        else:
            val = (frames[idx] - 128) << 8
            
        amp = abs(val)
        if amp > 1500:
            freq = int(amp / 12)
            if freq < 100: freq = 100
            if freq > 3000: freq = 3000
            out_notes.append(freq)
        else:
            out_notes.append(0)
            
    with outfile.open('wb') as f:
        f.write(b'SMUS')
        f.write(struct.pack('<I', len(out_notes)))
        for freq in out_notes:
            f.write(struct.pack('<H', freq))
            
    print(f"Extracted {len(out_notes)} music notation intervals -> {outfile}")
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv))
