"""Generate original synthetic AAC and verify the pinned Helix decoder, no packages needed."""
import argparse
import array
import hashlib
import json
import math
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "fixtures"
OUT = ROOT / "tests" / "out"
POSITIVE_FILES = tuple(f"stereo-{rate}.aac" for rate in (128,192,256))
NEGATIVE_FILES = ("reject-48k.aac", "reject-main.aac", "reject-malformed.aac", "reject-mono.aac",
                  "reject-truncated.aac", "stereo-faststart.m4a", "stereo-moov-last.m4a")

def required_fixtures(directory):
    paths = [directory / name for name in POSITIVE_FILES + NEGATIVE_FILES]
    missing = [p.name for p in paths if not p.is_file() or p.stat().st_size == 0]
    if missing:
        raise RuntimeError("Missing or empty required fixtures: " + ", ".join(missing) + "; regenerate with --generate")
    return paths

def run(args, expected=0):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode != expected:
        raise RuntimeError(f"{args}: exit {result.returncode}\n{result.stdout}\n{result.stderr}")
    return (result.stdout + "\n" + result.stderr).strip()

def generate():
    FIXTURES.mkdir(exist_ok=True)
    source = "aevalsrc=0.16*sin(2*PI*440*t)+if(eq(n\\,22050)\\,0.3\\,0)|0.16*sin(2*PI*997*t)+if(eq(n\\,66150)\\,0.3\\,0):s=44100:d=3"
    for bitrate in (128, 192, 256):
        run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", source,
             "-c:a", "aac", "-profile:a", "aac_low", "-b:a", f"{bitrate}k",
             "-f", "adts", FIXTURES / f"stereo-{bitrate}.aac"])
    for name, rate, channels, profile in (("reject-48k",48000,2,"aac_low"),
                                          ("reject-mono",44100,1,"aac_low")):
        run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i", source,
             "-ar", str(rate), "-ac", str(channels), "-c:a", "aac", "-profile:a", profile,
             "-b:a", "128k", "-f", "adts", FIXTURES / f"{name}.aac"])
    positive = FIXTURES / "stereo-192.aac"
    # Deliberately invalid fixture: advertise Main in the first ADTS header.
    # No Main encoder is claimed; rejection must precede compressed decoding.
    main_header = bytearray(positive.read_bytes()[:7])
    main_header[2] &= 0x3f
    (FIXTURES / "reject-main.aac").write_bytes(main_header)
    for name, flags in (("stereo-faststart.m4a", ["-movflags", "+faststart"]),
                        ("stereo-moov-last.m4a", [])):
        run(["ffmpeg", "-v", "error", "-y", "-i", positive, "-c:a", "copy", *flags, FIXTURES / name])
    (FIXTURES / "reject-truncated.aac").write_bytes(positive.read_bytes()[:-10])
    (FIXTURES / "reject-malformed.aac").write_bytes(b"not AAC or an MP4 file\n")

def pcm(path):
    samples = array.array("h")
    samples.frombytes(path.read_bytes())
    if sys.byteorder != "little": samples.byteswap()
    return samples

def tone_power(samples, channel, frequency):
    # Ignore the codec's first/last 0.2 s. Evaluate an exact-frequency projection.
    values = samples[channel::2][8820:-8820]
    phase = 2 * math.pi * frequency / 44100
    re = sum(v * math.cos(phase*i) for i,v in enumerate(values))
    im = sum(v * math.sin(phase*i) for i,v in enumerate(values))
    return re*re + im*im

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if args.generate: generate()
    paths = required_fixtures(FIXTURES)
    run([sys.executable, "-m", "unittest", "discover", "-s", "tests", "-p", "test_media_check.py"])
    if not args.skip_build:
        print(run(["cmd.exe", "/d", "/c", ROOT / "tests" / "build_host.cmd"]))
    OUT.mkdir(exist_ok=True)
    report = {"ffmpeg": run(["ffmpeg", "-version"]).splitlines()[0],
              "decoder": "arduino-libhelix v0.9.4, CYDPOD_AAC_LC_ONLY, MSVC x86",
              "scope": "generated synthetic files only; no target or user-media evidence", "files": []}
    report["allocation_test"] = run([OUT / "helix_decode.exe", "--allocation-test"])
    print(report["allocation_test"])
    for path in paths:
        item = {"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        if path.name in NEGATIVE_FILES:
            item["result"] = run([OUT / "helix_decode.exe", path, OUT / "rejected.s16le"], expected=4)
            item["expected_rejection"] = True
            print("PASS expected rejection", path.name)
        else:
            item["probe"] = json.loads(run(["ffprobe", "-v", "error", "-select_streams", "a:0",
                "-show_entries", "stream=codec_name,profile,sample_rate,channels,bit_rate:format=format_name,duration,bit_rate", "-of", "json", path]))
            stream = item["probe"]["streams"][0]
            if (stream["codec_name"], stream["profile"], stream["sample_rate"], stream["channels"]) != ("aac", "LC", "44100", 2):
                raise RuntimeError(f"Unexpected fixture profile: {path.name}: {stream}")
            actual = OUT / f"{path.stem}-helix.s16le"
            reference = OUT / f"{path.stem}-ffmpeg.s16le"
            item["result"] = run([OUT / "helix_decode.exe", path, actual])
            run(["ffmpeg", "-v", "error", "-y", "-i", path, "-f", "s16le", "-c:a", "pcm_s16le", reference])
            a, b = pcm(actual), pcm(reference)
            if len(a) != len(b) or len(a) <= 44100*2*3:
                raise RuntimeError(f"Decoded sample-count mismatch: {path.name}: {len(a)} vs {len(b)}")
            error = sum((x-y)**2 for x,y in zip(a,b))
            signal = sum(y*y for y in b)
            snr = 10*math.log10(signal/max(error,1))
            peak_error = max(abs(x-y) for x,y in zip(a,b))
            left_separation = 10*math.log10(tone_power(a,0,440)/max(tone_power(a,0,997),1))
            right_separation = 10*math.log10(tone_power(a,1,997)/max(tone_power(a,1,440),1))
            # Fixed-point/PNS differences are concentrated around the impulses.
            # Require >55 dB aggregate agreement, <0.8% full-scale peak error,
            # and independent channel identification, rather than bit identity.
            if not (snr > 55 and peak_error < 256 and min(left_separation,right_separation) > 35):
                raise RuntimeError(f"PCM comparison failed: {path.name}: SNR {snr}, peak {peak_error}, separation {left_separation}/{right_separation}")
            item.update(samples_per_channel=len(a)//2, snr_db=round(snr,2), max_sample_error=peak_error,
                        left_tone_separation_db=round(left_separation,2), right_tone_separation_db=round(right_separation,2))
            print(item["result"], f"SNR={snr:.2f}dB peak_error={peak_error}")
        report["files"].append(item)
    (FIXTURES / "verification.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    if len(report["files"]) != len(POSITIVE_FILES) + len(NEGATIVE_FILES):
        raise RuntimeError("Incomplete fixture verification matrix")
    print(f"PASS synthetic fixture matrix: {len(POSITIVE_FILES)} positive, {len(NEGATIVE_FILES)} negative; report fixtures/verification.json")

if __name__ == "__main__": main()
