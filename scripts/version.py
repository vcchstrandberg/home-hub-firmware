import subprocess
Import("env")


def git(args, default):
    try:
        return subprocess.check_output(
            ["git"] + args, stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return default


# Derive the firmware version from git at build time so a flash always embeds
# the true state of the tree — no hardcoded string to fall behind. Examples:
#   v2.3                   built exactly on tag v2.3
#   v2.3-2-g1a2b3c         2 commits after v2.3
#   v2.3-2-g1a2b3c-dirty   ...with uncommitted changes (you flashed unsaved work)
#   1a2b3c / 1a2b3c-dirty  no tags yet (falls back to the short commit sha)
# Tag a release with:  git tag -a v2.4 -m "v2.4"   (then git push --tags)
describe = git(["describe", "--tags", "--always", "--dirty"], "unknown")

# The UI adds its own "v" prefix, so strip a leading "v" to avoid "vv2.3".
app_version = describe[1:] if describe.startswith("v") else describe

# Short commit for the dedicated commit line on the boot splash.
commit = git(["rev-parse", "--short", "HEAD"], "unknown")

print("firmware version: %s (commit %s)" % (app_version, commit))

env.Append(CPPDEFINES=[
    ("APP_VERSION", env.StringifyMacro(app_version)),
    ("GIT_COMMIT",  env.StringifyMacro(commit)),
])
