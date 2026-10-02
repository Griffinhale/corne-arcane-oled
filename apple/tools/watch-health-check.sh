#!/usr/bin/env bash
# Exercise the watch adapter and actor on macOS without reading the health store.
set -euo pipefail
cd "$(dirname "$0")/../.."
swift build -c release --product city-check
build=$(swift build -c release --show-bin-path)
objects=()
while IFS= read -r object; do objects+=("$object"); done < <(find "$build/CityKit.build" "$build/CCorneArcaneCity.build" -name '*.o')
swiftc -parse-as-library -O -I "$build/Modules" -I "$build/CCorneArcaneCity.build" \
    apple/WatchApp/HealthReducer.swift apple/WatchApp/WatchCityDriver.swift \
    apple/Tests/WatchHealthCheck.swift "${objects[@]}" -o "$build/watch-health-check"
"$build/watch-health-check"
