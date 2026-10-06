import SwiftUI

// Keep runtime availability in one place; the project still builds with Xcode 26.
extension View {
    func compatibleOnChange<Value: Equatable>(of value: Value, initial: Bool = false,
                                               _ action: @escaping (Value, Value) -> Void) -> some View {
        onChange(of: value) { [value] newValue in action(value, newValue) }
            .onAppear { if initial { action(value, value) } }
    }

    @ViewBuilder
    func compatibleGlass<S: Shape>(_ shape: S, tint: Color? = nil) -> some View {
        if #available(iOS 26.0, *) {
            if let tint { glassEffect(.regular.tint(tint), in: shape) }
            else { glassEffect(.regular, in: shape) }
        } else {
            // Pre-Liquid-Glass: material over the live Metal drawable. On the
            // first frame the drawable is often still clear, so callers bump
            // `liquidGlassEpoch` after appear to rebuild this background once
            // the game has presented.
            background(.ultraThinMaterial, in: shape)
                .overlay { shape.fill(tint?.opacity(0.25) ?? .clear).allowsHitTesting(false) }
        }
    }

    @ViewBuilder
    func compatibleGlassButton(prominent: Bool = false) -> some View {
        if #available(iOS 26.0, *) {
            if prominent { buttonStyle(.glassProminent) }
            else { buttonStyle(.glass) }
        } else {
            if prominent { buttonStyle(.borderedProminent) }
            else { buttonStyle(.bordered) }
        }
    }

    @ViewBuilder
    func compatiblePresentationBackground() -> some View {
        if #available(iOS 16.4, *) { presentationBackground(.regularMaterial) }
        else { self }
    }

    @ViewBuilder
    func compatibleBounce<Value: Equatable>(value: Value) -> some View {
        if #available(iOS 17.0, *) { symbolEffect(.bounce, value: value) }
        else { self }
    }
}
