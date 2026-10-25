# MinimalAAC v1 specification

Build a reusable, internal-RAM AAC-LC decoder for classic ESP32/CYD, independent
of any playback hardware or framework task. Decode directly from seekable SD
storage through an absolute-offset callback and return caller-owned, native-rate
16-bit mono/stereo PCM blocks. Include Arduino and ESP-IDF examples.

Persistent component RAM, including one stereo PCM frame, must remain below
48 KiB and independent of track length. Use a 2 KiB compressed-packet buffer,
fixed table caches and a caller-owned codec arena. Do not allocate or create
tasks inside the component. Keep all M4A sample/chunk tables on the source.

Support ordinary AAC-LC M4A with metadata before/after the media, standard
sample/chunk tables, 32/64-bit offsets and box sizes, simple rate-1 edits and
the final media duration. Also accept single-block ADTS AAC. Bound profiles,
channels, packet sizes and timeline support as documented in the README.

Validate with the user's 44.1 kHz stereo, ~320 kb/s Madness track, generated
mono/stereo fixtures, a long file, container variants and invalid inputs.
Compare decoded audio with FFmpeg with tolerance for fixed-point differences
and final padding. Report compiler-derived ESP32 memory and object sizes,
stack estimates separately, and whether hardware validation occurred.

The source package, examples, provenance, licence files, reproducible checks
and validation evidence form the deliverable. Bluetooth/DAC transport,
resampling, arbitrary seeking and integration into another repository are
outside this implementation.
