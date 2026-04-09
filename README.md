# IIITA Pokerbots

## Dependencies
 - python>=3.5
 - cython (pip install cython)
 - eval7 (pip install eval7)
 - Java>=8 for java_skeleton
 - C++17 for cpp_skeleton
 - boost for cpp_skeleton (`sudo apt install libboost-all-dev`)

## Participant Submission Guide

This section is the end-to-end guide for participants submitting bots through PRs.

### 1. Understand What CI Checks

Every PR is validated by the qualification gatekeeper in `.github/workflows/submission-qualification.yml`.

For each changed submission bot:

1. Path format is validated.
2. Required files are validated.
3. A qualification match is run against the baseline bot.
4. The submission passes only if the final submission bankroll is at least the threshold.

Current defaults used in CI:

- baseline bot: `python_skeleton`
- qualification rounds (hands): `300`
- pass threshold: submission bankroll `>= 1`

### 2. Allowed Submission Paths

Your bot must be inside one of these directories:

- `submission/<roll_no>/python_bot/`
- `submission/<roll_no>/cpp_bot/`

Examples:

- `submission/IIT2024235/python_bot/`
- `submission/IIT2024235/cpp_bot/`

Any other directory shape under `submission/` is treated as invalid.

### 3. Required Files

#### Python bot (`submission/<roll_no>/python_bot/`)

Required:

1. `commands.json`
2. `player.py`

Practical requirement for runtime compatibility:

1. Include the `skeleton/` package next to `player.py`.

Minimal `commands.json`:

```json
{
	"build": [],
	"run": ["python3", "player.py"]
}
```

#### C++ bot (`submission/<roll_no>/cpp_bot/`)

Required:

1. `commands.json`
2. One build indicator file: `build.sh` or `Makefile` or `CMakeLists.txt`

### 4. Build and Run Contract (`commands.json`)

`commands.json` must be valid JSON and must contain:

1. `build`: JSON array
2. `run`: JSON array (must not be empty)

The runner executes these commands inside your bot directory.

### 5. Local Submission Checklist (Before PR)

1. Ensure your bot is at the correct path under `submission/<roll_no>/...`.
2. Ensure `commands.json` is valid JSON.
3. Ensure your bot runs without interactive input.
4. Ensure runtime dependencies are vendored or available in the expected environment.
5. Ensure there are no accidental changes outside your submission folder.

### 6. Local Qualification Run (Recommended)

Run the same gatekeeper script used by CI:

```bash
python scripts/tournament/qualification_gatekeeper.py \
	--repo-root . \
	--base-ref origin/main \
	--baseline-path python_skeleton \
	--num-rounds 300 \
	--min-submission-bankroll 1 \
	--output-dir .qualification
```

Outputs are written to:

1. `.qualification/summary.md`
2. `.qualification/results.json`
3. `.qualification/logs/`

### 7. Security Rules You Must Follow

To protect fairness, PRs are rejected if baseline files are modified.

Do not change baseline paths such as:

1. `python_skeleton/`
2. Any path configured as protected baseline path in gatekeeper arguments

The gatekeeper also materializes the baseline from the trusted base ref, not from PR-modified files.

### 8. Common Rejection Reasons

1. Invalid path under `submission/`
2. Missing `commands.json`
3. Missing `player.py` for Python bot
4. Invalid `commands.json` schema
5. Runtime/build failure during isolated match
6. Submission bankroll below threshold
7. Protected baseline path modified in PR

### 9. PR Tips

1. Keep your PR focused on submission files only.
2. Include a short description of strategy and expected behavior.
3. If CI fails, inspect `.qualification` artifacts from the workflow and iterate.

## Automated Tournament Pipeline

This repository includes a two-stage tournament flow:

1. PR-time qualification in GitHub Actions.
2. Post-deadline static round robin run manually.

### Phase 1: PR Qualification (Gatekeeper)

Workflow file: `.github/workflows/submission-qualification.yml`

Submission paths must follow one of:

- `submission/<roll_no>/python_bot/`
- `submission/<roll_no>/cpp_bot/`

Each changed submission is validated for required files and then matched against a baseline bot in an isolated temporary sandbox.

Default qualification parameters:

- baseline bot: `python_skeleton`
- rounds per qualification match: `300`
- minimum submission bankroll to pass: `>= 1`

The workflow posts a sticky PR comment with:

- per-submission validation outcome
- bankroll results vs baseline
- pass/fail verdict

It also uploads `.qualification/` as an artifact with logs and JSON summaries.

### Phase 2: Static Round Robin (Finals)

Run manually after the deadline:

```bash
python scripts/tournament/run_round_robin.py \
	--repo-root . \
	--submissions-root submission \
	--baseline-path python_skeleton \
	--qualification-rounds 300 \
	--qualification-threshold 1 \
	--match-rounds 600 \
	--output-dir tournament_results
```

This script:

1. Discovers all bots under `submission/<roll_no>/(python_bot|cpp_bot)`.
2. Re-validates and re-qualifies bots against baseline.
3. Runs every unique finals pair among qualified bots.
4. Tracks total bankroll and W/L/D statistics.
5. Writes outputs:
	 - `tournament_results/qualification.csv`
	 - `tournament_results/matches.csv`
	 - `tournament_results/results.csv`
	 - detailed logs in `tournament_results/logs/`

### Security and Isolation Notes

- Untrusted bot code is run inside the GitHub Actions runner (CI) or the local execution host.
- Every match runs in a fresh temporary sandbox directory that contains only:
	- `engine.py`
	- an auto-generated `config.py`
	- copied bot directories for the two participants
- Engine game clock and player timeouts remain enforced via `config.py`.
