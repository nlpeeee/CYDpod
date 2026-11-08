# Trimmed Bluedroid

Rebuilds Arduino-ESP32 3.3.10's ESP-IDF libraries for the esp32 without the Bluetooth features CYDpod never uses:
- BLE, BLE Mesh and BluFi;
- HFP;
- the dual-mode controller (Classic only now);
- half-size BTC/BTU task stacks.

Spec: [docs/specs/trimmed-bluedroid.md](../../docs/specs/trimmed-bluedroid.md). Results: [docs/research/bluedroid-trim.md](../../docs/research/bluedroid-trim.md).

| File | Purpose |
|---|---|
| `defconfig.cydpod-bt` | The only configuration change: Bluetooth options applied to Espressif's shipped `sdkconfig` |
| `patches/*.patch` | Bluedroid source patches applied to the trim build only (0001: forward the speaker's volume report) |
| `build-libs.ps1` | Builds ESP-IDF v5.5.4 twice in one directory, stock and trimmed, with the pinned xtensa toolchain |
| `make-libs.py` | Copies Espressif's package and replaces only the artefacts that differ between the two builds (`libbt.a`, `libesp_hid.a`, `libprotocomm.a`, `sdkconfig`, the two `sdkconfig.h`) |
| `use-libs.ps1` | Switches the Arduino core between stock and trimmed |

## Rebuild

You need Windows PowerShell 5.1, Git, Python 3.12 and Visual Studio Build Tools (for cmake and ninja).

```powershell
git -c core.longpaths=true clone --branch v5.5.4 --depth 1 --recurse-submodules --shallow-submodules https://github.com/espressif/esp-idf.git .tools/esp-idf-5.5.4
py -3.12 -m venv .tools/idf-venv
.tools/idf-venv/Scripts/python -m pip install -r .tools/esp-idf-5.5.4/tools/requirements/requirements.core.txt
powershell -NoProfile -File tools/bluedroid/build-libs.ps1
python tools/bluedroid/make-libs.py E:/dev/CYDpod E:/dev/CYDpod/.tools/esp32-libs-cydpod-bt
powershell -NoProfile -File tools/bluedroid/use-libs.ps1 trim
powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel bluedroid-trim
powershell -NoProfile -File tools/bluedroid/use-libs.ps1 stock
```

`dependencies.lock.json` still names Espressif's stock package. Normal builds and the 0.5.2 build kit use stock until the trimmed stack is accepted on the board.
