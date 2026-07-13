# Security

CouchPlay uses a root system service for Linux account management, input-device ownership, shared game directories, runtime access, and Gamescope session lifecycle operations. This maintained version treats the D-Bus interface as a security boundary.

## Privileged helper model

- Production helper calls require a recognized local administrative D-Bus caller and a matching Polkit authorization.
- User targets are limited to the calling regular user or dedicated CouchPlay-managed regular users, depending on the operation.
- Caller-supplied file and directory operations use fixed allowlists, ownership checks, and descriptor-based traversal that rejects symlinks and path escapes.
- Shared directory mounts are restricted to caller-owned game-library roots and helper-derived targets beneath managed user homes.
- Process stop and kill operations apply only to transient units tracked by CouchPlay.
- Input ownership, mounts, runtime ACLs, and per-session audio access are tracked and cleaned up when sessions end.
- Game commands are passed as direct arguments and run as the selected regular user. They are not evaluated by a root shell.

## Installation

Build an exact reviewed commit locally, run the complete tests, inspect the staged install, and elevate only the verified local artifacts. Do not pipe a mutable remote installer into a shell.

The helper intentionally retains significant privileges required for its documented functions. Systemd confinement is defense in depth and does not replace validation at the D-Bus boundary.

## Verification

The repository includes hostile-input regression tests for helper authorization, user targeting, path traversal, ACL scope, mount targets, and tracked process control. Run:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure --parallel 1
```

Use bounded build parallelism because the current test layout compiles shared source files into multiple test executables.

## Updates and reporting

Security review applies only to the exact commit tested. Review helper, D-Bus, Polkit, installer, dependency, and service-definition changes before updating.

When reporting a possible vulnerability, avoid publishing credentials, private system data, or a working exploit in a public issue. Contact the repository maintainer privately or use GitHub private vulnerability reporting when available.
