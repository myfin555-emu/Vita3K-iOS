import SwiftUI

/// The settings screen.
///
/// Deliberately plain: a `Form` in a `NavigationStack`, built from stock
/// `Toggle`/`Slider`/`Picker` rows. On iOS 26 that is what produces correct
/// Liquid Glass — the navigation bar and any sheet chrome are glass, and the
/// content underneath is opaque and scrolls beneath it. Hand-rolling glass
/// behind list rows (which the UIKit screen used to do) both fights the design
/// system and costs a refraction pass per visible row.
///
/// Everything here is a system control, so Dynamic Type, VoiceOver labels and
/// values, high-contrast, and reduce-transparency are all inherited rather than
/// reimplemented.
@MainActor
struct SettingsView: View {
    @StateObject private var model: SettingsModel
    @ObservedObject private var runtimeLatch = RuntimeLatch.shared
    @AppStorage("tsubomi.orientationLockEnabled")
    private var orientationLockEnabled = false
    @AppStorage("tsubomi.orientationLock")
    private var orientationLock = OrientationLockOption.portrait.rawValue
    @AppStorage(NormalListArtwork.defaultsKey)
    private var normalListArtwork = NormalListArtwork.coverArt.rawValue
    @AppStorage(LibrarySortOption.defaultsKey)
    private var librarySort = LibrarySortOption.alphabetical.rawValue
    /// Invoked when the user is done; the host controller dismisses.
    private let onFinish: () -> Void

    @State private var showingResetConfirmation = false
    @State private var showingRuntimeNotice = false
    @State private var showingDefaultReset = false

    init(scope: SettingsModel.Scope, onFinish: @escaping () -> Void) {
        _model = StateObject(wrappedValue: SettingsModel(scope: scope))
        self.onFinish = onFinish
    }

    var body: some View {
        NavigationStack {
            Form {
                if !model.isPerGame && runtimeLatch.revealed {
                    runtimeSection
                }
                if !model.isPerGame {
                    Section { NavigationLink("JIT") { JITSettingsView() } }
                    generalSection
                    librarySection
                }
                videoSection
                graphicsSection
                speedSection
                modulesSection
                audioSection
                if !model.isPerGame {
                    controlsSection
                    performanceOverlaySection
                    firmwareSection
                }
                if model.isPerGame {
                    perGameResetSection
                }
            }
            .safeAreaInset(edge: .bottom) {
                if !model.isPerGame {
                    Button("Reset to Default Settings", role: .destructive) {
                        showingDefaultReset = true
                    }
                    .buttonStyle(.bordered)
                    .padding(.horizontal)
                    .padding(.vertical, 8)
                }
            }
            .animation(.easeOut(duration: 0.2), value: model.isPerGame)
            .navigationTitle(model.navigationTitle)
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .principal) {
                    Text(model.navigationTitle)
                        .font(.headline)
                        .lineLimit(1)
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") {
                        model.save()
                        // Per-game settings persist immediately in the native
                        // frontend. Global settings play after the core reports
                        // that its commit completed.
                        if model.isPerGame {
                            HomeSoundEffects.play(.sparkle)
                        }
                        onFinish()
                    }
                    .disabled(!model.canSave)
                }
            }
            .alert("Developer mode enabled", isPresented: $showingRuntimeNotice) {
                Button("OK", role: .cancel) {}
            }
            .confirmationDialog("Reset all settings to their defaults?", isPresented: $showingDefaultReset, titleVisibility: .visible) {
                Button("Reset", role: .destructive) {
                    model.resetToDefaults()
                    model.save()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This resets the lightweight iOS settings. Game files and saves are not affected.")
            }
        }
    }

    // MARK: - Sections

    private var generalSection: some View {
        Section {
            DefaultsToggle("Interface sound effects", key: .soundEffects)
            DefaultsToggle("Liquid Glass", key: .liquidGlassInGame)
            Toggle("Orientation lock", isOn: $orientationLockEnabled)
                .compatibleOnChange(of: orientationLockEnabled) { _, isEnabled in
                    Bridge.setOrientationLockEnabled(isEnabled)
                }
            if orientationLockEnabled {
                Picker("Locked orientation", selection: $orientationLock) {
                    ForEach(OrientationLockOption.allCases) { option in
                        Text(option.title).tag(option.rawValue)
                    }
                }
                .compatibleOnChange(of: orientationLock) { _, newValue in
                    Bridge.applyOrientationLock(newValue)
                }
            }
        } header: {
            Text("General")
        } footer: {
            Text("Liquid Glass is turned off in game only.")
        }
    }

    private var runtimeSection: some View {
        Section {
            Button {
                exportGames()
            } label: {
                Label("Export games", systemImage: "square.and.arrow.up")
            }
            Button {
                importGames()
            } label: {
                Label("Import games…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Developer")
        } footer: {
            Text("Exporting a large library takes a few minutes. A share sheet opens when the archive is ready.")
        }
    }

    private func exportGames() {
        model.save()
        onFinish()
        LibraryStateBridge.setBusy("Exporting games…")
        Bridge.exportLibraryArchive()
    }

    private func importGames() {
        model.save()
        onFinish()
        Bridge.presentLibraryArchiveImportPicker()
    }

    private var videoSection: some View {
        Section("Video") {
            Toggle("V-Sync", isOn: $model.vSync)
                .accessibilityHint("Synchronizes presentation to the display.")
                .compatibleOnChange(of: model.vSync) { _, _ in model.save() }
            Toggle("Texture cache", isOn: $model.textureCache)
            Toggle("Shader cache", isOn: $model.shaderCache)
                .accessibilityHint("Reuses compiled shaders between sessions. Turn off to force regeneration when diagnosing a graphics fault.")
        }
    }

    private var graphicsSection: some View {
        Section {
            LabeledContent("Resolution") {
                Text(model.resolutionLabel)
                    .foregroundStyle(.secondary)
                    .monospacedDigit()
            }
            Slider(value: $model.resolutionMultiplier, in: 0.5...2.0, step: 0.25) {
                Text("Resolution multiplier")
            } minimumValueLabel: {
                Text("0.5×").font(.caption2)
            } maximumValueLabel: {
                Text("2×").font(.caption2)
            }
            .accessibilityValue(model.resolutionLabel)
            .compatibleOnChange(of: model.resolutionMultiplier) { _, _ in model.save() }

            Toggle("High accuracy", isOn: $model.highAccuracy)
                .compatibleOnChange(of: model.highAccuracy) { _, _ in model.save() }
            Toggle("Surface sync", isOn: $model.surfaceSync)
            Toggle("Double buffer", isOn: $model.doubleBuffer)
            Toggle("Async pipeline compilation", isOn: $model.asyncPipelineCompilation)

            Picker("Anisotropic filtering", selection: $model.anisotropicFiltering) {
                ForEach(SettingsModel.anisotropicOptions, id: \.self) { value in
                    Text(SettingsModel.anisotropicLabel(value)).tag(value)
                }
            }
        } header: {
            Text("Graphics")
        } footer: {
            Text("""
                Graphics look wrong? Try High accuracy (shader interlock). \
                Surface sync is separate: leave it off on iOS for speed unless \
                lighting or effects look wrong. Character models shattered? \
                Make sure Double buffer is off.
                """)
        }
    }

    private var speedSection: some View {
        Section {
            Toggle("FPS Hack", isOn: $model.fpsHack)
            if !model.isPerGame {
                Toggle("Turbo mode (iOS)", isOn: $model.turboMode)
            }
        } header: {
            Text("Speed & Timing")
        } footer: {
            if !model.isPerGame {
                Text("Turbo mode prioritizes emulation, rendering and shader workers using iOS scheduling. It does not force GPU clocks or bypass thermal limits. Higher priority may increase power use.")
            }
            Text("FPS Hack reduces multi-vblank waits to one. Some 30 FPS games can reach 60 FPS; others may run too fast or show timing problems. Turn it off if this happens. It does not increase GPU power.")
        }
    }

    private var modulesSection: some View {
        Section {
            Picker("Modules mode", selection: $model.modulesMode) {
                Text("Automatic").tag(0)
                Text("Automatic + manual").tag(1)
                Text("Manual").tag(2)
            }
            if model.modulesMode != 0 {
                NavigationLink("Select firmware modules (\(model.lleModules.count))") {
                    Form {
                        if model.availableModules.isEmpty {
                            Text("Install PSVUPDAT.PUP to populate the firmware module list.")
                        }
                        ForEach(model.availableModules, id: \.self) { name in
                            Toggle(name, isOn: Binding(
                                get: { model.lleModules.contains(name) },
                                set: { model.setModule(name, enabled: $0) }
                            ))
                        }
                    }
                    .navigationTitle("Firmware modules")
                }
            }
        } header: {
            Text("Modules")
        } footer: {
            Text("Automatic chooses firmware modules for you. Automatic + manual adds your selections. Manual uses your selected firmware modules. Changes apply on the next game launch.")
        }
    }

    private var audioSection: some View {
        Section {
            Toggle("NGS audio", isOn: $model.ngsAudio)
            LabeledContent("Audio backend", value: "SDL")
            LabeledContent("Volume", value: "\(Int(model.audioVolume))%")
            Slider(value: $model.audioVolume, in: 0...100, step: 1) {
                Text("Audio volume")
            }
        } header: {
            Text("Audio")
        } footer: {
            Text("NGS is full Vita audio emulation; disable it only while diagnosing a problem.")
        }
    }

    private var controlsSection: some View {
        Section {
            Button("Virtual controls…") {
                Bridge.presentControllerOptions()
            }
            DefaultsToggle("Colored face buttons", key: .coloredFaceButtons)
            NavigationLink("Face button layout") {
                FaceButtonLayoutView(model: model)
            }
        } header: {
            Text("Controls")
        } footer: {
            Text("Virtual controls covers opacity, scale, layout, visibility, and physical-pad auto-hide. Colored face buttons tint the on-screen ✕ ○ □ △ glyphs. Some third-party controllers report face buttons in Xbox-style positions; remap them if the wrong button responds.")
        }
    }

    private var performanceOverlaySection: some View {
        Section {
            DefaultsToggle("Show FPS", key: .perfFPS, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime", key: .perfFrametime, onEnable: enablePerfOverlay)
            DefaultsToggle("Show frametime graph", key: .perfFrametimeGraph, onEnable: enablePerfOverlay)
            DefaultsToggle("Show RAM usage", key: .perfRAM, onEnable: enablePerfOverlay)
            DefaultsToggle("Show battery %", key: .perfBattery, onEnable: enablePerfOverlay)
            DefaultsToggle("Collect diagnostic logs", key: .collectLogs, onChange: Bridge.applyLoggingPreference)
            DefaultsToggle("Show live log", key: .perfLog, onEnable: enablePerfOverlay)
        } header: {
            Text("Performance overlay")
        } footer: {
            Text("The overlay appears in-game once any metric is enabled. The live log keeps the last ~250 lines, which is useful when reporting a bug.")
        }
    }

    private var librarySection: some View {
        Section {
            DefaultsToggle("Show title IDs", key: .showTitleIDs, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show version number", key: .showVersion, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Show game size", key: .showGameSize, onChange: Bridge.reloadLibraryCells)
            DefaultsToggle("Wide cover art", key: .wideCoverArt)
            DefaultsToggle("Compact list", key: .compactList)
            Picker("Normal list artwork", selection: $normalListArtwork) {
                ForEach(NormalListArtwork.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            Picker("Sort games by", selection: $librarySort) {
                ForEach(LibrarySortOption.allCases) { option in
                    Text(option.title).tag(option.rawValue)
                }
            }
            .pickerStyle(.menu)
            .compatibleOnChange(of: librarySort) { _, newValue in
                LibraryState.shared.setSortOption(rawValue: newValue)
            }
            Button {
                model.save()
                onFinish()
                LibraryStateBridge.setBusy("Exporting saves…")
                Bridge.exportAllSaves()
            } label: {
                Label("Export all game saves", systemImage: "square.and.arrow.up")
            }
            Button {
                model.save()
                onFinish()
                Bridge.presentAllSaveImportPicker()
            } label: {
                Label("Import all game saves…", systemImage: "square.and.arrow.down")
            }
        } header: {
            Text("Library")
        } footer: {
            Text("Choose cover art or the square game icon for the normal list. Compact list always uses the game icon. All-save archives include save data, trophy progress, playtime, and last-played dates. Import replaces included progress; other games remain unchanged.")
        }
    }

    private func enablePerfOverlay() {
        Bridge.performanceOverlayDidEnableMetric()
    }

    private var firmwareSection: some View {
        Section {
            LabeledContent("Firmware") {
                Text(model.firmwareVersion.isEmpty ? "Not installed" : model.firmwareVersion)
                    .foregroundStyle(.secondary)
            }
            .contentShape(Rectangle())
            .onTapGesture {
                if runtimeLatch.record(0x6D4E_13B7) {
                    showingRuntimeNotice = true
                }
            }
            if !model.firmwareReady && !model.missingFirmware.isEmpty {
                Label(model.missingFirmware, systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.secondary)
            }
            LabeledContent("Version", value: AppInfo.versionDisplay)
                .contentShape(Rectangle())
                .onTapGesture {
                    if runtimeLatch.record(0xA29C_508D) {
                        showingRuntimeNotice = true
                    }
                }
            NavigationLink("What's New") {
                ChangelogView()
            }
            Button {
                Bridge.openBugReportForm()
            } label: {
                Text("Report a bug")
                    .foregroundStyle(.red)
            }
            Button {
                Bridge.shareLogFile()
            } label: {
                Label("Share log file", systemImage: "square.and.arrow.up")
            }
            Button("Forked from Vita3K") {
                Bridge.open(urlString: "https://github.com/Vita3K/Vita3K")
            }
            Button("Developed by @halcyonpalace") {
                Bridge.open(urlString: "https://x.com/halcyonpalace")
            }
        } header: {
            Text("About")
        }
    }

    private var perGameResetSection: some View {
        Section {
            Button("Use global settings", role: .destructive) {
                showingResetConfirmation = true
            }
            .confirmationDialog(
                "Remove this game's custom settings?",
                isPresented: $showingResetConfirmation,
                titleVisibility: .visible
            ) {
                Button("Use global settings", role: .destructive) {
                    model.resetPerGameOverrides()
                    onFinish()
                }
            } message: {
                Text("This game will follow the global settings the next time it launches.")
            }
        } footer: {
            Text("These settings apply only to this game and take effect the next time it launches.")
        }
    }
}

enum OrientationLockOption: String, CaseIterable, Identifiable {
    case portrait
    case landscape
    case landscapeFlipped

    var id: String { rawValue }

    var title: String {
        switch self {
        case .portrait: "Portrait"
        case .landscape: "Landscape"
        case .landscapeFlipped: "Landscape (Flipped)"
        }
    }
}

@MainActor
private struct FaceButtonLayoutView: View {
    @ObservedObject var model: SettingsModel

    private static let positions = ["Bottom", "Right", "Left", "Top"]

    var body: some View {
        Form {
            Section {
                picker("Cross", selection: $model.bindCross)
                picker("Circle", selection: $model.bindCircle)
                picker("Square", selection: $model.bindSquare)
                picker("Triangle", selection: $model.bindTriangle)
            } footer: {
                Text("Choose which physical button position triggers each Vita button.")
            }
        }
        .navigationTitle("Face Buttons")
        .navigationBarTitleDisplayMode(.inline)
    }

    private func picker(_ label: String, selection: Binding<Int>) -> some View {
        Picker(label, selection: selection) {
            ForEach(Array(Self.positions.enumerated()), id: \.offset) { index, name in
                Text(name).tag(index)
            }
        }
    }
}
