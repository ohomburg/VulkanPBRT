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

configs = {
    # "reference": {"--spp": 1024, "--vptLimit": 2**31 - 1},
    # "limit512": {"--spp": 256, "--vptLimit": 512},
    # "limit768": {"--spp": 256, "--vptLimit": 768},
    # "limit1024": {"--spp": 256, "--vptLimit": 1024},
    # "limit1536": {"--spp": 256, "--vptLimit": 1536},
    # "limit2048": {"--spp": 256, "--vptLimit": 2048},
    # "limitMax": {"--spp": 256, "--vptLimit": 2**31 - 1},
    "alpha1e-1": {"--denoiser": "asvgf", "--tempAlpha": "0.1"},
    "alpha5e-2": {"--denoiser": "asvgf", "--tempAlpha": "0.05"},
    "alpha1e-2": {"--denoiser": "asvgf", "--tempAlpha": "0.01"},
    "alpha5e-3": {"--denoiser": "asvgf", "--tempAlpha": "0.005"},
}

for name, overrides in configs.items():
    for i, base_args in enumerate(cases):
        outdir = Path(os.getcwd(), name)
        outdir.mkdir(parents=True, exist_ok=True)
        outname = Path(outdir, f"perf_{i}.csv")

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
