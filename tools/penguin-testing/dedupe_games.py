import json
import sys

# This script is intended to use the output of `print-gdrom-metadata <game library folder>`
# to determine which games to test. It will only test the main region for each game, and only
# the first disc for each game. This is to avoid testing the same game multiple times with
# different regions or multiple discs

def get_region(disc):
  region = None
  if 'U' in disc['area_symbols']: region = 'U'
  if 'E' in disc['area_symbols']: region = 'E'
  if 'J' in disc['area_symbols']: region = 'J'
  assert region is not None
  return region

# Make sure a json file path is provided
if len(sys.argv) != 2:
  print(f'Usage: python {sys.argv[0]} <disc_metadata.json>')
  sys.exit(1)

disc_meta = json.load(open(sys.argv[1]))

# Go over every game and assign a canonical/main region to it for testing
game_regions = {}
for disc in disc_meta:
  region = get_region(disc)
  name = disc['software_name']

  if name not in game_regions:
    game_regions[name] = region
  elif region > game_regions[name]:
    game_regions[name] = region

# Form the list of games to test, only the main region for each game, and only disc 1
testing_games = []
for disc in disc_meta:
  name = disc['software_name']

  if game_regions[name] != get_region(disc):
    # print(f'Skipping {disc['filename']} because it is not the main region for this game')
    continue

  # Skip discs that are not disc 1
  if 'GD-ROM1/' not in disc['device_info']:
    # print(f'Skipping {disc['filename']} because it is not the first disc for this game')
    continue

  testing_games.append(disc)

for disc in testing_games:
  print(disc['filename'])

print(f'Testing {len(testing_games)} games')