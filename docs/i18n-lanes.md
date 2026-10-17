# Literals not yet converted

The English literals the QML still shows without a string id (78 at
0.1.0-beta.1), grouped by the lane that owns each file, with the id in
`strings.json` to use where the design has one. It is the output of
`python tools/i18n/lint_strings.py --report`, whose allowlist is
`tools/i18n/lint_allowlist.json`: regenerate it rather than editing it, as the
line numbers drift. The lane names are the branches the work was split across
when the lint was written; most have since merged.

### experience lane (feature/app-experience)

| file | line | literal | id in strings.json |
|---|---|---|---|
| `CompleteScreen.qml` | 160 | METHOD | none: needs a string |
| `SettingsScreen.qml` | 231 | Shader style for the 3D cube shown around the app. | none: needs a string |
| `SettingsScreen.qml` | 247 | CUBE MOTION | none: needs a string |
| `SettingsScreen.qml` | 251 | Auto-rotate when idle | none: needs a string |
| `SettingsScreen.qml` | 267 | RENDERING | none: needs a string |
| `SettingsScreen.qml` | 438 | Test turn | none: needs a string |
| `StatsOverlay.qml` | 63 | FPS  | none: needs a string |

### kit session (feature/ax-kit)

| file | line | literal | id in strings.json |
|---|---|---|---|
| `SettingsState.qml` | 70 | System | none: needs a string |
| `SettingsState.qml` | 71 | Light | `settings.theme.light` |
| `SettingsState.qml` | 72 | Dark | `settings.theme.dark` |
| `SettingsState.qml` | 88 | Auto | none: needs a string |
| `SettingsState.qml` | 25 | Physically-Based Realism | none: needs a string |
| `SettingsState.qml` | 25 | Instructional Glow | none: needs a string |
| `SettingsState.qml` | 25 | Borderlands Cel-Shading | none: needs a string |

### scan lane (feature/direct-scan)

| file | line | literal | id in strings.json |
|---|---|---|---|
| `DirectScanScreen.qml` | 69 | Camera unavailable | none: needs a string |
| `DirectScanScreen.qml` | 71 | Cube read | `scan.title.complete` |
| `DirectScanScreen.qml` | 73 | Starting the camera… | `scan.title.starting` |
| `DirectScanScreen.qml` | 74 |  of 6 sides read | none: needs a string |
| `DirectScanScreen.qml` | 80 | All six sides make a valid cube. Working out the solution… | `scan.guide.complete` |
| `DirectScanScreen.qml` | 84 | Setting the camera's exposure and colour balance… | `scan.guide.settling` |
| `DirectScanScreen.qml` | 90 | These six sides fit together in more than one way. Turn the cube slowly so that two sides show at once. | `scan.guide.ambiguous` |
| `DirectScanScreen.qml` | 93 | These six sides do not make a valid cube yet. Show each side again, square to the camera. | `scan.guide.conflict` |
| `DirectScanScreen.qml` | 95 | Reading this side — hold it there | none: needs a string |
| `DirectScanScreen.qml` | 97 | Hold it still | `scan.guide.inView` |
| `DirectScanScreen.qml` | 99 | Hold one side of the cube square to the camera | `scan.guide.first` |
| `DirectScanScreen.qml` | 100 | Turn to a side that is not filled in below, square to the camera | `scan.guide.next` |
| `DirectScanScreen.qml` | 197 | Recording this scan to  | none: needs a string |
| `DirectScanScreen.qml` | 294 | Cancel | `common.cancel` |
| `DirectScanScreen.qml` | 300 | Start over | `scan.startOver` |

### solve lane (feature/solve-guidance)

| file | line | literal | id in strings.json |
|---|---|---|---|
| `SolveScreen.qml` | 21 | White cross | `stage.1.title` |
| `SolveScreen.qml` | 21 | Bring the four white edges home, matching each side centre. | none: needs a string |
| `SolveScreen.qml` | 22 | White corners | `stage.2.title` |
| `SolveScreen.qml` | 22 | Insert the four white corners to complete the first layer. | none: needs a string |
| `SolveScreen.qml` | 23 | Middle layer | `stage.3.title` |
| `SolveScreen.qml` | 23 | Send the middle-layer edges home. | none: needs a string |
| `SolveScreen.qml` | 24 | Orient last layer | none: needs a string |
| `SolveScreen.qml` | 24 | Orient the last layer's edges and corners so the top side reads solid. | none: needs a string |
| `SolveScreen.qml` | 25 | Permute last layer | none: needs a string |
| `SolveScreen.qml` | 25 | Permute the last layer's pieces into their solved positions. | none: needs a string |
| `SolveScreen.qml` | 179 | wrong | none: needs a string |
| `SolveScreen.qml` | 179 | That was not the move | none: needs a string |
| `SolveScreen.qml` | 180 | unknown | none: needs a string |
| `SolveScreen.qml` | 180 | That side is not expected | none: needs a string |
| `SolveScreen.qml` | 481 | STAGE  | none: needs a string |
| `SolveScreen.qml` | 481 |  OF  | none: needs a string |
| `SolveScreen.qml` | 485 | Solved | `complete.title` |
| `SolveScreen.qml` | 550 | Drag the cube to look round it | none: needs a string |
| `SolveScreen.qml` | 779 | Rescan the cube and carry on from there | none: needs a string |
| `SolveScreen.qml` | 805 | Only the view has turned. Keep your cube as it is. | none: needs a string |
| `SolveScreen.qml` | 970 | STAGES | none: needs a string |
| `SolveScreen.qml` | 1023 | Previous | `solution.previous` |
| `SolveScreen.qml` | 1056 | Out of step? Rescan | none: needs a string |
| `SolveScreen.qml` | 1100 | Solving… | `settings.section.solving` |
| `SolveScreen.qml` | 134 |  on top,  | none: needs a string |
| `SolveScreen.qml` | 135 |  facing you | none: needs a string |
| `SolveScreen.qml` | 153 | Camera unavailable | none: needs a string |
| `SolveScreen.qml` | 155 | Starting the camera… | `scan.title.starting` |
| `SolveScreen.qml` | 161 | Matched to your cube | none: needs a string |
| `SolveScreen.qml` | 165 | Matched. The camera cannot see the side facing you: press Next after this move. | none: needs a string |
| `SolveScreen.qml` | 166 | Matched. Make the move and it steps on by itself. | none: needs a string |
| `SolveScreen.qml` | 167 | That side looks the same turned. Show another to tell which way up. | none: needs a string |
| `SolveScreen.qml` | 168 | Not the move that was asked for | none: needs a string |
| `SolveScreen.qml` | 169 | Not the side that was expected | none: needs a string |
| `SolveScreen.qml` | 171 | Show a side again if you turn the cube over | none: needs a string |
| `SolveScreen.qml` | 172 | Hold any side square to the camera and the cube here turns to match | none: needs a string |
| `SolveScreen.qml` | 186 |  was turned, but not the way shown. Turn it back and try again. | none: needs a string |
| `SolveScreen.qml` | 188 | ) was turned instead. Turn it back, then make the move shown. | none: needs a string |
| `SolveScreen.qml` | 191 | The side facing the camera is not what the solution has there. If the last move went wrong and cannot be put right, rescan. | none: needs a string |
| `SolveScreen.qml` | 213 | Left side | none: needs a string |
| `SolveScreen.qml` | 213 | Right side | none: needs a string |
| `SolveScreen.qml` | 215 | half turn | `amount.half` |
| `SolveScreen.qml` | 215 | quarter turn | `amount.quarter` |
| `SolveScreen.qml` | 241 | Seen from behind | none: needs a string |

