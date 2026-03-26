---
description: Workflow Index — Summary of all available agent workflows
---

# TizenClaw-Assets Agent Workflows

This directory contains the workflow documents referenced by the agent.
When adding a new workflow, you must also update this list.

> [!IMPORTANT]
> Workflow documents must only be created or modified after the corresponding
> feature has been fully verified (build, deploy, and runtime validation) on
> an actual device. Writing workflow documents for unverified features is prohibited.

## Workflow List

| Slash Command | File | Description |
|---|---|---|
| `/AGENTS` | [`../../AGENTS.md`](../../AGENTS.md) | Main development workflow (project root) |
| `/code_review` | `code_review.md` | Code review checklist and Review-Fix loop (max 5 iterations) |
| `/gbs_build` | `gbs_build.md` | Execute Tizen gbs build and verify build results |
| `/deploy_to_emulator` | `deploy_to_emulator.md` | Deploy RPM to emulator/device via sdb |
| `/cli_testing` | `cli_testing.md` | Functional testing via tizenclaw-ocr |
| `/gtest_integration` | `gtest_integration.md` | gtest & ctest unit test configuration and execution |
| `/crash_debug` | `crash_debug.md` | Crash dump debugging (sdb shell + gdb) |
| `/coding_rules` | `coding_rules.md` | Coding rules and style guide (Google C++ Style) |
| `/commit_guidelines` | `commit_guidelines.md` | Git commit message rules (Conventional Commits) |
| `/wsl_environment` | `wsl_environment.md` | WSL development environment guide (direct & PowerShell methods) |
