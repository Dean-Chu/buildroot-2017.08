/^# Packages using this file: / {
  s/# Packages using this file://
  ta
  :a
  s/ nano / nano /
  tb
  s/ $/ nano /
  :b
  s/^/# Packages using this file:/
}
