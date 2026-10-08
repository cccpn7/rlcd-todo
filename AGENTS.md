# Project instructions

## Product and phase

- Read `docs/product-plan.md` before product changes and `docs/development.md` before building.
- This is a personal desk task display for Waveshare ESP32-S3-RLCD-4.2: portrait 300×400, monochrome, normally USB powered.
- Three categories: 今日重点, 今日杂项, 今日追踪. Each may span multiple display pages. Manual ordering expresses priority. Unfinished tasks carry over automatically without changing category or order.
- The device only displays and pages through tasks. Editing and completion belong to the web interface.
- Auto-page through every category subpage at a 5-second dwell, including empty categories. Manual paging, home and successful publication restart the dwell; pairing pauses it. Use a nonblocking monotonic timer, never periodic retransmission.
- First validate web entry → Mac service → device; later connect the external assistant (大圣) through the same service interface.
- Do not add voice, focus timers, multiple users, public hosting, or automatic task extraction without an explicit scope change.
- Draft edits autosave; only explicit publication changes the screen. USB and BLE both belong to v1, USB takes priority after handshake. No Wi-Fi dependency.
- Keep original text intact. Portrait body is 18px with 24px line height; long items continue across pages. No ellipsis, hierarchy, or automatic shrinking.
- Rasterize text directly in monochrome at native resolution. The final physical display is rotated 180 degrees from the initial prototype, including overlays; logical previews stay upright.
- Mac owns the shared layout/font engine; firmware displays cached monochrome pages and overlays date/connection status. Official example compilation is not application completion.

## Collaboration and ownership

- The product owner decides real scenarios, priorities, and experience tradeoffs. Agents turn those decisions into designs, implementation, and evidence.
- Investigate technical facts before asking. Ask when a remaining ambiguity changes product behavior; do not guess that behavior silently.
- Make the smallest relevant change. Preserve other work; avoid speculative abstractions and unrelated cleanup.
- Firmware owns rendering, input, synchronization, and its last-good cache. The server owns task persistence and interfaces. The web UI and future assistant share that interface.
- Explain results in Chinese using plain language. Keep product flows free of unnecessary implementation detail.

## Verification

- State success criteria before substantive changes and run the narrowest useful checks.
- Report compilation, device communication, flashing, and physical-screen verification separately. Never infer one from another.
- The current official example uses landscape assumptions. Portrait work must verify lookup dimensions, buffer bounds, pixel mapping, and all four screen corners.
- Build through the documented scripts. Keep generated artifacts outside tracked source and retain reproducible dependency versions when adding application code.
- Do not flash as part of a documentation or repository task. For hardware work, follow the scope already authorized by the user.

## Public repository and Git

- Never commit real tasks, private work documents, credentials, Wi-Fi settings, device identifiers, host-specific absolute paths, raw API responses, or device logs.
- Store local configuration in `.env.local`; private notes, responses, and data in `local/`. Use placeholders in published examples.
- Save complete external API responses to an ignored local file before parsing when fields are needed later. Do not print secrets.
- Keep downloaded vendor trees, local validation links, dependency caches, and build output untracked. Record upstream URLs and revisions in documentation.
- Preserve third-party license notices when introducing third-party code. No project license is selected yet.
- Bootstrap is committed to `main`; subsequent features use short-lived branches and focused commits. Never force-push `main` or overwrite unrelated changes.
- Review staged content before any public push. Configure missing author identity locally for this repository, not globally.

## Documentation

- Keep README status truthful. Update the product plan and decision log when agreed behavior changes.
- Keep machine-specific observations in ignored local records; publish only sanitized, reusable development instructions.
- Follow the user's global progress-report instructions locally. Do not copy private paths or personal memories into this repository.
