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
    "reference": {"--vptLimit": 2 ** 31 - 1, "--spp": 1024},
    "fast": {"--denoiser": "asvgf", "--vptLimit": 1024, "--cloudReproPoints": 1, "--cloudStatSteps": 64, "--vptBundle": 1, "--atrousIters": 3, "--tempAlpha": "0.01"},
    "balanced": {"--denoiser": "asvgf"},
    "quality": {"--denoiser": "asvgf", "--vptLimit": 2 ** 31 - 1, "--cloudReproPoints": 1, "--cloudStatSteps": 256, "--vptBundle": 2, "--atrousIters": 5, "--tempAlpha": "0.01"},

    "bundle2": {"--denoiser": "asvgf", "--vptBundle": 2},
    "bundle3": {"--denoiser": "asvgf", "--vptBundle": 3},
    "bundle4": {"--denoiser": "asvgf", "--vptBundle": 4},
    "bundle2fast": {"--denoiser": "asvgf", "--vptLimit": 1024, "--vptBundle": 2},
    "bundle4fast": {"--denoiser": "asvgf", "--vptLimit": 1024, "--vptBundle": 4},

    "limit512": {"--vptLimit": 512} | set_spp(256),
    "limit768": {"--vptLimit": 768} | set_spp(256),
    "limit1024": {"--vptLimit": 1024} | set_spp(256),
    "limit1536": {"--vptLimit": 1536} | set_spp(256),
    "limit2048": {"--vptLimit": 2048} | set_spp(256),
    "limit768d": {"--denoiser": "asvgf", "--vptLimit": 768},
    "limit1024d": {"--denoiser": "asvgf", "--vptLimit": 1024},
    "limit1536d": {"--denoiser": "asvgf", "--vptLimit": 1536},
    "limit2048d": {"--denoiser": "asvgf", "--vptLimit": 2048},

    "statsteps16": {"--denoiser": "asvgf", "--cloudStatSteps": 16},
    "statsteps32": {"--denoiser": "asvgf", "--cloudStatSteps": 32},
    "statsteps64": {"--denoiser": "asvgf", "--cloudStatSteps": 64},
    "statsteps96": {"--denoiser": "asvgf", "--cloudStatSteps": 96},
    "statsteps128": {"--denoiser": "asvgf", "--cloudStatSteps": 128},
    "statsteps192": {"--denoiser": "asvgf", "--cloudStatSteps": 192},
    "statsteps256": {"--denoiser": "asvgf", "--cloudStatSteps": 256},
    "statsteps384": {"--denoiser": "asvgf", "--cloudStatSteps": 384},

    "repro1": {"--denoiser": "asvgf", "--cloudReproPoints": 1},
    "repro3": {"--denoiser": "asvgf", "--cloudReproPoints": 3},
    "repro5": {"--denoiser": "asvgf", "--cloudReproPoints": 5},

    "alpha1e-1": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.1"},
    "alpha5e-2": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.05"},
    "alpha1e-2": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.01"},
    "alpha5e-3": {"--denoiser": "asvgf", "--atrousIters": 0, "--tempAlpha": "0.005"},

    "iters0": {"--denoiser": "asvgf", "--atrousIters": 0},
    "iters1": {"--denoiser": "asvgf", "--atrousIters": 1},
    "iters2": {"--denoiser": "asvgf", "--atrousIters": 2},
    "iters3": {"--denoiser": "asvgf", "--atrousIters": 3},
    "iters4": {"--denoiser": "asvgf", "--atrousIters": 4},
    "iters5": {"--denoiser": "asvgf", "--atrousIters": 5},
    "iters6": {"--denoiser": "asvgf", "--atrousIters": 6},

    "filter_box3": {"--denoiser": "asvgf", "--atrousFilter": 1},
    "filter_gauss5": {"--denoiser": "asvgf", "--atrousFilter": 0},
    "filter_box5": {"--denoiser": "asvgf", "--atrousFilter": 2},
    "filter_sub": {"--denoiser": "asvgf", "--atrousFilter": 3},
    "filter_sub3": {"--denoiser": "asvgf", "--atrousFilter": 4},
    "filter_sub5": {"--denoiser": "asvgf", "--atrousFilter": 5},
}

for name, overrides in configs.items():
    disabled_cases = overrides.get("disabled", [])
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
        subprocess.run(["flip", "-r", Path(refdir, file.name), "-t", file, "-d", outdir, "-nexm", "-c",
                        file.with_suffix(".flip.csv")])
