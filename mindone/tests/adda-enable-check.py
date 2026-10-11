import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def function(source, name):
    match = re.search(r"^static [^\n]*\b" + name + r"\(", source, re.M)
    if not match:
        raise ValueError(f"Missing function: {name}")
    end = source.index("\n}", match.start()) + 2
    return source[match.start():end] + "\n"


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description="Check ADDA hw_params preserves DAPM playback enable")
    parser.add_argument("--source", type=Path, default=root / "modules/snd_soc_mt6789_afe/mt6789-dai-adda.c")
    parser.add_argument("--scenario", choices=("initial", "reroute", "all"), default="all")
    args = parser.parse_args()
    source = args.source.read_text()
    header = root / "modules/snd_soc_mt6789_afe/mt6789-reg.h"
    common = (root / "modules/include/sound/soc/mediatek/common/mtk-base-afe.h").read_text()
    tokens = ("UL_IIR_SW", "AUDIO_SDM_LEVEL_NORMAL", "AUDIO_SDM_2ND",
              "MTK_AFE_ADDA_DL_RATE_8K", "MTK_AFE_ADDA_UL_RATE_8K",
              "MTK_AFE_ADDA_DL_GAIN_NORMAL")
    enums = []
    for token in tokens:
        matches = [m.group() for m in re.finditer(r"enum \{.*?\n\};", source + common, re.S)
                   if re.search(r"\b" + token + r"\b", m.group())]
        if len(matches) != 1:
            raise ValueError(f"Expected one enum for {token}")
        enums.append(matches[0])
    widget = re.search(r'SND_SOC_DAPM_SUPPLY_S\("ADDA Playback Enable",\s*\w+,\s*'
                       r'(\w+),\s*(\w+),\s*0,', source)
    if not widget:
        raise ValueError("Missing production playback enable widget")
    defines = re.search(r"^#define SDM_AUTO_RESET_THRESHOLD .+$", source, re.M).group()
    code = "\n".join(enums) + "\n" + defines + "\n"
    code += f"#define DAPM_REG {widget[1]}\n#define DAPM_SHIFT {widget[2]}\n"
    for name in ("adda_dl_rate_transform", "adda_ul_rate_transform", "mtk_dai_adda_hw_params"):
        code += function(source, name)
    fixture = Path(__file__).with_name("adda-enable.c").read_text()
    fixture = fixture.replace("PRODUCTION_FUNCTIONS", code)
    with tempfile.TemporaryDirectory(prefix="adda-enable-") as directory:
        c_file = Path(directory) / "fixture.c"
        binary = Path(directory) / "fixture"
        c_file.write_text(fixture)
        subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                        "-fno-strict-overflow", "-fsanitize=address,undefined",
                        "-include", str(header), str(c_file), "-o", str(binary)],
                       check=True, timeout=30)
        raise SystemExit(subprocess.run([str(binary), args.scenario], timeout=10).returncode)


if __name__ == "__main__":
    main()
