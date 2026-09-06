import Foundation
import Translation
import Darwin

private func writeLine(_ value: String) {
  let oneLine = value
    .replacingOccurrences(of: "\r", with: " ")
    .replacingOccurrences(of: "\n", with: " ")
  print(oneLine)
  fflush(stdout)
}

@available(macOS 26.0, *)
private struct AppleTranslationService {
  private let session: TranslationSession

  init() {
    session = TranslationSession(
      installedSource: Locale.Language(identifier: "zh-Hans"),
      target: Locale.Language(identifier: "en"))
  }

  func translate(_ text: String) async -> String {
    do {
      guard await session.isReady else {
        return ""
      }
      return try await session.translate(text).targetText
    } catch {
      // A missing language pack must never block typing.
      return ""
    }
  }
}

@main
struct Fcitx5TranslationHelper {
  static func main() async {
    guard #available(macOS 26.0, *) else {
      while readLine(strippingNewline: true) != nil {
        writeLine("")
      }
      return
    }

    let service = AppleTranslationService()
    while let line = readLine(strippingNewline: true) {
      if line == "__fcitx5_translation_ping__" {
        writeLine("__fcitx5_translation_pong__")
        continue
      }
      writeLine(await service.translate(line))
    }
  }
}
