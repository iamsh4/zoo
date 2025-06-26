from pyquery import PyQuery as pq
import re
import sys

page = pq(filename='sh_insns.html')
instructions = page('div.col_cont')

for instruction in instructions:
  components = [page(instruction).find('.col_cont_%s' % i).text() for i in range(6) ][1:]
  components = map(lambda line: re.sub( '\s+', ' ', line ).strip(), components)

  # We only care about SH4
  if 'SH4' not in components[0]: continue

  sys.stdout.write(components[1] + '\t')
  sys.stdout.write(components[2] + '\t')
  sys.stdout.write(components[3] + '\t')
  sys.stdout.write(components[4] + '\t')
  sys.stdout.write('\n')



