# Collecting a Deye support report

During startup or when connecting a newly configured device, **Save probe report…** is offered if the inverter has no supported profile, its initial register reads fail, or the selected logger cannot be reached after discovery finishes. A failed saved-address attempt first proceeds to broadcast and network discovery without a report offer. The report preserves the existing identity, raw readings, protocol frames and error without sending additional requests. Open the offer, enter the exact model, and save the existing evidence. **Collect more data** is optional. The file is shared manually; nothing is sent automatically.

Once a complete telemetry snapshot succeeds, the offer is cleared and later connection losses do not show it again. Dismissing or saving the report suppresses repeated offers for that startup attempt. There is no permanent probe entry in Settings. The probe dialog opens directly from the startup problem banner and uses the selected logger's saved connection settings.

1. Enter the complete inverter model printed on its label.
2. Optionally note readings displayed by the inverter at the same time: battery SOC, battery power and direction, solar power, grid power, home load and AC voltage. Include the operating conditions if known.
3. Optionally choose **Collect more data** and wait for its result. Connection recovery and normal telemetry polling are paused while it runs.
4. Save the JSON report and send it to the maintainer yourself. A partial or unsuccessful report can still explain why a logger or model is unsupported.

Opening the window does not start a probe. Nothing is uploaded automatically.

## What it collects

The report contains the application version, collection times, the entered model and notes, available logger/inverter firmware metadata, profile detection results, raw register reads, and errors. A supported profile also supplies its measurements. Every read keeps its address and count so the data can be interpreted again without contacting the inverter.

The initial inverter read covers registers 0–24 and does not require a matching profile. If a profile matches, the probe reads its existing documented ranges. If no profile matches, the default report contains the identity evidence and the reason detection failed.

For an unsupported device, **Read known-family ranges** optionally collects the bounded read ranges already used by the bundled Deye profiles. This can reveal whether the device shares an existing map. It does not search every register or claim that a different family's decoded measurements apply to the device. Reads are sequential, limited to 32 registers per request and bounded by an overall deadline. Unsupported reads are recorded rather than discarded. A final identity read checks that the same inverter answered throughout the collection.

Only Modbus FC03 reads through the configured Solarman V5 logger are used. The probe does not change inverter settings, restart devices, modify firmware, or discover other hosts. Loggers using another local protocol may provide only metadata and an explanatory connection error in this version.

## Sharing and identifiers

Reports omit known device serial numbers, MAC addresses and local IP addresses by default. Serial-number registers are removed from raw data, and wire frames are omitted because they also contain identifiers. Known identifiers in notes and error messages are redacted too; review any other personal information you type into the notes.

**Include device serial numbers and network addresses** includes those identifiers and the captured protocol frames when needed for transport diagnosis. Logger web credentials and Wi-Fi configuration are never collected into a report.

The report is saved locally. The application has no report-upload service.

## Command line

The same collector is built into the desktop executable and can run without a display:

```bash
./bin/deye-monitor-qt --probe --no-settings \
  --host 192.168.1.100 --serial 1234567890 \
  --probe-model SUN-EXACT-MODEL > deye-probe.json
```

Omit `--serial` to obtain the logger serial from its status page. Without `--no-settings`, the saved connection and web credentials are used. Add `--probe-notes` for reference readings, `--probe-extended` for the optional known-family reads, or `--include-identifiers` for a full device-identifying report. Those flags apply only to `--probe`.

## How the report becomes support

A report is evidence, not an automatic register map. We compare the exact model and firmware, raw values, contemporaneous display readings and available protocol documentation. A genuinely different map can require a follow-up set of targeted reads.

Once the mapping is established, a new JSON profile can define its identity signature, read plan, units, signedness and scaling. The captured responses provide regression fixtures, so adding that profile does not require rebuilding the dashboard UI.
