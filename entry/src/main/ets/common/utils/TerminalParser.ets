/**
 * Utility for parsing terminal output data.
 */
export class TerminalParser {
  /**
   * Cleans ANSI escape codes and normalizes newlines/tabs from raw terminal text.
   */
  static parseTerminalData(text: string): string[] {
    // 1. Remove ANSI color/control codes, including OSC and BELL characters
    const ansiRegex =
      /\x1B\[[\x30-\x3F]*[\x20-\x2F]*[\x40-\x7E]|\x1B\][^\x07\x1B]*(?:\x07|\x1B\\)|\x1B[\x20-\x2F]*[\x40-\x7E]|\x07/g;
    let cleanText = text.replace(ansiRegex, "");

    // 2. Replace tabs with 4 spaces
    cleanText = cleanText.replace(/\t/g, "    ");

    // 3. Handle backspaces (\b, which is \x08)
    while (cleanText.includes("\x08")) {
      cleanText = cleanText.replace(/[^\x08]\x08/g, "");
      cleanText = cleanText.replace(/^\x08+/g, "");
    }

    // 4. Standardize newlines (convert \r\n to \n first)
    cleanText = cleanText.replace(/\r\n/g, "\n");

    // 5. Split by '\n' and then process '\r'
    let lines = cleanText.split("\n");
    lines = lines.map((line) => {
      // If line contains standalone '\r', simulate carriage return (keep text after last '\r')
      const rIndex = line.lastIndexOf("\r");
      if (rIndex !== -1) {
        return line.substring(rIndex + 1);
      }
      return line;
    });

    return lines;
  }
}
