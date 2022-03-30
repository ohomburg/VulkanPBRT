import subprocess
import sys
import os
from pathlib import Path

# glorified shell script that runs a bunch of test cases.

exe_path = sys.argv[1]
data_path = sys.argv[2]

base_config_1 = {"i": 1940, "-f": 1, "--spp": 1, "cam": 1}

# each line is one test case
cases = [
    # basic spatial denoise test
    base_config_1 | {"export": "t0"},
    # convergence
    base_config_1 | {"--spp": 128, "export": "t1"},
    # moving denoise (1spp) test
    base_config_1 | {"-f": 2, "export": "t2_%d"},
    # moving denoise test with movement halfway through
    base_config_1 | {"-f": 2, "--spp": 32, "export": "t3_%d"},
    # moving denoise test with converged first frame
    base_config_1 | {"-f": 129, "cam": 2, "export": "t4_%d"},
    # moving denoise path over multiple cameras, smaller data size
    {"i": 1198, "-f": 10, "--spp": 1, "cam": 3, "export": "t5_%d"},
]


# convenience function to set spp and disable superfluous test cases
def set_spp(spp):
    return {"--spp": spp, "disabled": [1, 3, 4]}


configs = {
    "reference": {"--vptLimit": 2**31 - 1, "--spp": 1024},
    "limit512": {"--vptLimit": 512} | set_spp(256),
    "limit768": {"--vptLimit": 768} | set_spp(256),
    "limit1024": {"--vptLimit": 1024} | set_spp(256),
    "limit1536": {"--vptLimit": 1536} | set_spp(256),
    "limit2048": {"--vptLimit": 2048} | set_spp(256),
    "alpha1e-1": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.1"},
    "alpha5e-2": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.05"},
    "alpha1e-2": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.01"},
    "alpha5e-3": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.005"},
}

for name, overrides in configs.items():
    disabled_cases = configs.get("disabled", [])
    for i, base_args in enumerate(cases):
        if i in disabled_cases:
            continue

        outdir = Path(os.getcwd(), name)
        outdir.mkdir(parents=True, exist_ok=True)
        outname = Path(outdir, f"out_{i}.txt")

        # merge arguments
        config = base_args | overrides

        # path arguments that need processing
        args = ["-i", f"{data_path}cloud-{config['i']}.xyz",
                "--cam", f"{os.getcwd()}/test{config['cam']}.json",
                "--exportIllumination", f"{outdir}/{config['export']}.exr"]

        # rest of the arguments
        for key in config.keys():
            if key.startswith("-"):
                args += [key, str(config[key])]

        # exec command with output redirection
        with open(outname, 'w') as outfile:
            outfile.write(" ".join(args) + "\n")
            outfile.flush()
            done = subprocess.run([exe_path] + args, stdout=outfile, cwd=os.path.dirname(exe_path))
            if done.returncode != 0:
                print("Subprocess returned", hex(done.returncode), " ".join(args))

refdir = Path(os.getcwd(), "reference")

# run flip for quality comparison
for name, overrides in configs.items():
    outdir = Path(os.getcwd(), name)
    for file in outdir.glob("*.exr"):
        subprocess.run(["flip", "-r", Path(refdir, file.name), "-t", file, "-d", outdir, "-nexm", "-c", file.with_suffix(".flip.csv")])
