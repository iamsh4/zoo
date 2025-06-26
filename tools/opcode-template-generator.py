import re

codes = open('patterns.txt').readlines()
codes = map(lambda s: s.replace('\n', ''), codes)
codes = map(lambda s: re.sub(r'\s+', ' ', s), codes)
for code in codes:
  pattern, mneumonic = code.split('|')
  print 'OPCODE(%s, 0u, NO_FLAGS, "%s", {' % (pattern, mneumonic)
  print '  throw std::runtime_error("Unimplemented opcode \'%s\' (%s)");' % (mneumonic, pattern)
  print '});'
  print ''
