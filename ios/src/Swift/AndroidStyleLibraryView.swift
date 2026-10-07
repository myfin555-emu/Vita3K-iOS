import SwiftUI
import UIKit

/// Android-style library chrome for iOS.
///
/// The emulator state and bridge remain shared with the existing frontend.
/// This view only changes presentation: toolbar actions, search, sorting,
/// grid/list switching, import FAB, and per-game actions.
@MainActor
struct AndroidStyleLibraryView: View {
    @ObservedObject private var library = LibraryState.shared
    @State private var searchText = ""
    @State private var searching = false
    @State private var showFilter = false
    @State private var showOverflow = false
    @State private var selectedIDs: Set<String> = []
    @State private var selectionMode = false
    @State private var showBatchDelete = false
    @State private var infoGame: GameEntry?
    @State private var renameGame: GameEntry?

    private var visibleGames: [GameEntry] {
        let query = searchText.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !query.isEmpty else { return library.orderedGames }
        return library.orderedGames.filter {
            $0.displayTitle.localizedCaseInsensitiveContains(query)
                || $0.titleID.localizedCaseInsensitiveContains(query)
        }
    }

    var body: some View {
        NavigationStack {
            ZStack(alignment: .bottomTrailing) {
                content
                addButton
            }
            .navigationTitle(selectionMode ? "\(selectedIDs.count) selected" : "Tsubomi")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { toolbar }
            .searchable(text: $searchText, isPresented: $searching, placement: .automatic, prompt: "Search games")
            .refreshable {
                Bridge.refreshLibrary()
            }
            .sheet(isPresented: $showFilter) {
                LibraryFilterSheet()
            }
            .sheet(item: $infoGame) { game in
                GameInfoSheet(game: game)
            }
            .sheet(item: $renameGame) { game in
                RenameSheet(game: game) { library.refreshAfterRename() }
            }
            .confirmationDialog(
                "Delete \(selectedIDs.count) selected game\(selectedIDs.count == 1 ? "" : "s")?",
                isPresented: $showBatchDelete,
                titleVisibility: .visible
            ) {
                Button("Delete", role: .destructive) {
                    for id in selectedIDs {
                        Bridge.delete(titleID: id)
                    }
                    selectedIDs.removeAll()
                    selectionMode = false
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("Installed game content is removed. Saves and trophies are kept.")
            }
            .overlay { busyOverlay }
        }
        .background(Color(.systemBackground).ignoresSafeArea())
        .onAppear {
            library.focusLayout = library.isListMode ? .list : .grid
        }
    }

    @ViewBuilder
    private var content: some View {
        if library.orderedGames.isEmpty {
            ContentUnavailableView(
                "No Games",
                systemImage: "gamecontroller",
                description: Text("Tap + to import a game")
            )
        } else if visibleGames.isEmpty {
            ContentUnavailableView.search(text: searchText)
        } else if library.isListMode {
            List {
                ForEach(visibleGames) { game in
                    gameRow(game)
                }
            }
            .listStyle(.plain)
        } else {
            ScrollView {
                LazyVGrid(
                    columns: [GridItem(.adaptive(minimum: 148), spacing: 14)],
                    spacing: 18
                ) {
                    ForEach(visibleGames) { game in
                        gameCard(game)
                    }
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 12)
            }
        }
    }

    private func gameCard(_ game: GameEntry) -> some View {
        Button {
            if selectionMode {
                toggleSelection(game)
            } else {
                launch(game)
            }
        } label: {
            GameCard(game: game)
                .overlay(alignment: .topTrailing) {
                    if selectedIDs.contains(game.titleID) {
                        Image(systemName: "checkmark.circle.fill")
                            .font(.title2)
                            .symbolRenderingMode(.palette)
                            .foregroundStyle(.white, Color.accentColor)
                            .padding(8)
                    }
                }
        }
        .buttonStyle(.plain)
        .opacity(library.firmwareReady ? 1 : 0.55)
        .contextMenu { actions(for: game) }
    }

    private func gameRow(_ game: GameEntry) -> some View {
        Button {
            if selectionMode {
                toggleSelection(game)
            } else {
                launch(game)
            }
        } label: {
            GameRow(game: game)
                .overlay(alignment: .trailing) {
                    if selectedIDs.contains(game.titleID) {
                        Image(systemName: "checkmark.circle.fill")
                            .foregroundStyle(.tint)
                    }
                }
        }
        .buttonStyle(.plain)
        .opacity(library.firmwareReady ? 1 : 0.55)
        .contextMenu { actions(for: game) }
    }

    @ToolbarContentBuilder
    private var toolbar: some ToolbarContent {
        if selectionMode {
            ToolbarItem(placement: .topBarLeading) {
                Button("Cancel") {
                    selectedIDs.removeAll()
                    selectionMode = false
                }
            }
            ToolbarItemGroup(placement: .topBarTrailing) {
                Button {
                    selectedIDs = Set(visibleGames.map(\.titleID))
                } label: {
                    Image(systemName: "checkmark.circle")
                }
                .accessibilityLabel("Select all visible games")
                Button {
                    showBatchDelete = true
                } label: {
                    Image(systemName: "trash")
                }
                .disabled(selectedIDs.isEmpty)
            }
        } else {
            ToolbarItemGroup(placement: .topBarTrailing) {
                Button {
                    searching = true
                } label: {
                    Image(systemName: "magnifyingglass")
                }
                Button {
                    showFilter = true
                } label: {
                    Image(systemName: "line.3.horizontal.decrease.circle")
                }
                Menu {
                    Button {
                        selectionMode = true
                    } label: {
                        Label("Select games", systemImage: "checkmark.circle")
                    }
                    Button {
                        Bridge.refreshLibrary()
                    } label: {
                        Label("Refresh library", systemImage: "arrow.clockwise")
                    }
                    Button {
                        Bridge.presentGlobalSettings()
                    } label: {
                        Label("Settings", systemImage: "gearshape")
                    }
                    Divider()
                    Button {
                        Bridge.openBugReportForm()
                    } label: {
                        Label("Report a bug", systemImage: "exclamationmark.bubble")
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .accessibilityLabel("More")
            }
        }
    }

    private var addButton: some View {
        Button {
            Bridge.presentGameImportPicker()
        } label: {
            Image(systemName: "plus")
                .font(.title2.weight(.semibold))
                .frame(width: 56, height: 56)
        }
        .buttonStyle(.borderedProminent)
        .clipShape(Circle())
        .shadow(radius: 6)
        .padding(.trailing, 20)
        .padding(.bottom, 18)
        .accessibilityLabel("Import game")
    }

    @ViewBuilder
    private var busyOverlay: some View {
        if let message = library.busyMessage {
            ZStack {
                Color.black.opacity(0.35).ignoresSafeArea()
                ProgressView(message)
                    .padding(24)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 18))
            }
        }
    }

    @ViewBuilder
    private func actions(for game: GameEntry) -> some View {
        Button {
            launch(game)
        } label: {
            Label("Launch", systemImage: "play.fill")
        }
        Button {
            infoGame = game
        } label: {
            Label("Game info", systemImage: "info.circle")
        }
        Button {
            renameGame = game
        } label: {
            Label("Rename", systemImage: "pencil")
        }
        Button {
            Bridge.presentSettings(forTitle: game.titleID, displayName: game.displayTitle)
        } label: {
            Label("Game settings", systemImage: "slider.horizontal.3")
        }
        Button {
            selectionMode = true
            selectedIDs.insert(game.titleID)
        } label: {
            Label("Select", systemImage: "checkmark.circle")
        }
        Button(role: .destructive) {
            Bridge.delete(titleID: game.titleID)
        } label: {
            Label("Delete", systemImage: "trash")
        }
    }

    private func toggleSelection(_ game: GameEntry) {
        if selectedIDs.contains(game.titleID) {
            selectedIDs.remove(game.titleID)
        } else {
            selectedIDs.insert(game.titleID)
        }
    }

    private func launch(_ game: GameEntry) {
        guard Bridge.firmwareReadyOrPresentAlert() else { return }
        guard library.jitAvailable else {
            Bridge.presentJITRequiredAlert()
            return
        }
        guard library.beginLaunch(game) else { return }
        Bridge.launch(titleID: game.titleID)
    }
}

@MainActor
private struct LibraryFilterSheet: View {
    @ObservedObject private var library = LibraryState.shared
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            Form {
                Section("View") {
                    Picker("Layout", selection: Binding(
                        get: { library.isListMode },
                        set: { library.isListMode = $0 }
                    )) {
                        Text("List").tag(true)
                        Text("Grid").tag(false)
                    }
                    .pickerStyle(.segmented)
                }
                Section("Sort") {
                    ForEach(LibrarySortOption.allCases) { option in
                        Button {
                            library.setSortOption(rawValue: option.rawValue)
                        } label: {
                            HStack {
                                Text(option.title)
                                Spacer()
                                if library.sortOption == option {
                                    Image(systemName: "checkmark")
                                        .foregroundStyle(.tint)
                                }
                            }
                        }
                        .foregroundStyle(.primary)
                    }
                }
            }
            .navigationTitle("Filter & sort")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }
}

@MainActor
private struct GameInfoSheet: View {
    let game: GameEntry
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section("Game") {
                    LabeledContent("Title", value: game.displayTitle)
                    LabeledContent("Title ID", value: game.titleID)
                    LabeledContent("Version", value: game.versionText)
                    LabeledContent("Size", value: game.sizeText)
                }
                Section("Play history") {
                    LabeledContent("Play time", value: game.playedTimeText)
                    LabeledContent("Last played", value: game.lastPlayedText)
                }
                if game.trophiesTotal > 0 {
                    Section("Trophies") {
                        LabeledContent("Unlocked", value: "\(game.trophiesUnlocked)/\(game.trophiesTotal)")
                    }
                }
            }
            .navigationTitle("Game info")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                }
            }
        }
        .presentationDetents([.medium, .large])
    }
}
