import Foundation
import Combine

/// Editable state behind the settings screen.
///
/// ObservableObject keeps settings bindings functional on iOS 16 and later.
@MainActor
final class SettingsModel: ObservableObject {
    /// What this screen is editing. Per-game hides the rows that are device
    /// properties rather than per-title preferences, and writes an override
    /// instead of committing to the global configuration.
    enum Scope: Equatable {
        case global
        /// - Parameters:
        ///   - titleID: the title being overridden.
        ///   - displayName: shown in the navigation title.
        case perGame(titleID: String, displayName: String)
    }

    let scope: Scope

    @Published var resolutionMultiplier: Float
    @Published var vSync: Bool
    @Published var shaderCache: Bool
    @Published var fpsHack: Bool
    @Published var turboMode: Bool
    @Published var modulesMode: Int
    @Published var lleModules: [String]
    let availableModules: [String]
    @Published var audioVolume: Double
    @Published var textureCache: Bool
    @Published var emulatorRAMText: String
    @Published var cpuOptimizations: Bool
    @Published var ngsAudio: Bool
    @Published var asyncPipelineCompilation: Bool
    @Published var precompileShadersBeforeLaunch: Bool
    @Published var anisotropicFiltering: Int
    @Published var highAccuracy: Bool
    @Published var surfaceSync: Bool
    @Published var doubleBuffer: Bool

    @Published var bindCross: Int
    @Published var bindCircle: Int
    @Published var bindSquare: Int
    @Published var bindTriangle: Int

    let firmwareVersion: String
    let firmwareReady: Bool
    let missingFirmware: String

    /// Firmware/controller-binding fields the screen shows but never edits are
    /// kept here so they can be written back untouched — the core is handed a
    /// whole settings value, so a dropped field would clear real state.
    private let original: EmulatorSettings

    init(scope: Scope) {
        self.scope = scope

        let settings: EmulatorSettings
        switch scope {
        case .global:
            settings = Bridge.currentSettings
        case .perGame(let titleID, _):
            settings = Bridge.settings(forTitle: titleID)
        }
        original = settings

        resolutionMultiplier = settings.resolutionMultiplier
        vSync = settings.vSync
        shaderCache = settings.shaderCache
        fpsHack = settings.fpsHack
        turboMode = settings.turboMode
        modulesMode = min(2, max(0, settings.modulesMode))
        lleModules = settings.lleModules
        availableModules = Array(Set(settings.availableModules + settings.lleModules)).sorted()
        audioVolume = Double(min(100, max(0, settings.audioVolume)))
        textureCache = settings.textureCache
        emulatorRAMText = String(settings.emulatorRAMMB)
        cpuOptimizations = settings.cpuOptimizations
        ngsAudio = settings.ngsAudio
        asyncPipelineCompilation = settings.asyncPipelineCompilation
        precompileShadersBeforeLaunch = settings.precompileShadersBeforeLaunch
        anisotropicFiltering = settings.anisotropicFiltering
        highAccuracy = settings.highAccuracy
        surfaceSync = settings.surfaceSync
        doubleBuffer = settings.doubleBuffer
        bindCross = settings.bindCross
        bindCircle = settings.bindCircle
        bindSquare = settings.bindSquare
        bindTriangle = settings.bindTriangle
        firmwareVersion = settings.firmwareVersion
        firmwareReady = settings.firmwareReady
        missingFirmware = settings.missingFirmware
    }

    private func wholeNumber(_ input: String, in range: ClosedRange<Int>) -> Int? {
        let text = input.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty, text.utf8.allSatisfy({ $0 >= 48 && $0 <= 57 }),
              let value = Int(text), range.contains(value) else { return nil }
        return value
    }

    var validEmulatorRAMMB: Int? { wholeNumber(emulatorRAMText, in: 512...2048) }
    var canSave: Bool { isPerGame || validEmulatorRAMMB != nil }

    func setModule(_ name: String, enabled: Bool) {
        lleModules.removeAll { $0 == name }
        if enabled { lleModules.append(name) }
    }

    var isPerGame: Bool {
        if case .perGame = scope { return true }
        return false
    }

    var navigationTitle: String {
        switch scope {
        case .global: return "Settings"
        case .perGame(_, let displayName): return displayName
        }
    }

    /// Writes the edits back through the bridge.
    func save() {
        let settings = original.copy() as! EmulatorSettings
        settings.resolutionMultiplier = resolutionMultiplier
        settings.vSync = vSync
        settings.shaderCache = shaderCache
        settings.fpsHack = fpsHack
        if !isPerGame { settings.turboMode = turboMode }
        settings.modulesMode = modulesMode
        settings.lleModules = lleModules
        settings.audioVolume = Int(audioVolume)
        settings.textureCache = textureCache
        if !isPerGame, let ram = validEmulatorRAMMB {
            settings.emulatorRAMMB = ram
        }
        settings.cpuOptimizations = cpuOptimizations
        settings.ngsAudio = ngsAudio
        settings.asyncPipelineCompilation = asyncPipelineCompilation
        settings.precompileShadersBeforeLaunch = precompileShadersBeforeLaunch
        settings.anisotropicFiltering = anisotropicFiltering
        settings.highAccuracy = highAccuracy
        settings.surfaceSync = surfaceSync
        settings.doubleBuffer = doubleBuffer

        switch scope {
        case .global:
            // Controller bindings describe the attached hardware, so they are
            // only editable (and only written) in the global scope.
            settings.bindCross = bindCross
            settings.bindCircle = bindCircle
            settings.bindSquare = bindSquare
            settings.bindTriangle = bindTriangle
            Bridge.apply(settings)
        case .perGame(let titleID, _):
            Bridge.apply(settings, forTitle: titleID)
        }
    }

    /// Drops this title's overrides so it follows the global settings again.
    func resetPerGameOverrides() {
        guard case .perGame(let titleID, _) = scope else { return }
        Bridge.resetSettings(forTitle: titleID)
    }

    // MARK: - Value formatting

    /// Anisotropic filtering is a power-of-two factor, presented as discrete
    /// steps rather than a free slider because only these values are valid.
    static let anisotropicOptions: [Int] = [1, 2, 4, 8, 16]

    static func anisotropicLabel(_ value: Int) -> String {
        value <= 1 ? "Off" : "\(value)×"
    }

    var resolutionLabel: String {
        String(format: "%.2f×", resolutionMultiplier)
    }
}
