import os
import sys
import subprocess

PENGUIN_TEST_EXE = './build-tup/penguin-game-test'
PENGUIN_TEST_EXE = os.path.abspath(PENGUIN_TEST_EXE)

# First run tup on the test exe
subprocess.run(['tup', './build-tup/penguin-game-test'], shell=True)

# Make the reports folder
os.makedirs('reports', exist_ok=True)

# Create a path object corresponding to the folder in argv[1]
path = os.path.abspath(os.path.expanduser(sys.argv[1]))

# Create a list of full paths for all the '.chd' files in that folder
chd_files = [os.path.join(path, f) for f in os.listdir(path) if f.endswith('.chd')]

# For each '.chd' file, create a corresponding folder in the reports folder without the '.chd' extension
tests = []
for chd_file in chd_files:
    folder = os.path.join('reports', os.path.splitext(os.path.basename(chd_file))[0])
    os.makedirs(folder, exist_ok=True)

    # Make sure the folder is a full path
    folder = os.path.abspath(folder)

    # Append the file and the location the report for that file should go to
    tests.append((chd_file, folder))

# Sort tests by name of the chd file
tests.sort(key=lambda x: x[0])

# For each test, run (PENGUIN_TEST_EXE, '-no-limit', '-stop-after 300', '-disc ' + chd_file]) and pipe
# the output of the process to stdout.txt in the report folder for that test. Additionally move any generated
# screenshot-*.ppm files to the report folder for that test. Note: The test program must be run from 
# the current directory as it expects to find certain files relative to this path
for chd_file, folder in tests:
  # If there is already a stdout.txt file in the folder, skip this test
  if os.path.exists(os.path.join(folder, 'stdout.txt')):
    print('Skipping ' + chd_file + ' because stdout.txt already exists in ' + folder)
    continue

  with open(os.path.join(folder, 'stdout.txt'), 'w') as f:
    print('Running ' + chd_file + ' and saving output to ' + folder)
    subprocess.run([PENGUIN_TEST_EXE, '-no-limit', '-stop-after', f'{60*30}', '-disc', chd_file], stdout=f, shell=False)
    for f in os.listdir('.'):
      if f.startswith('screenshot-') and f.endswith('.ppm'):
        os.rename(f, os.path.join(folder, f))
        print('Moved ' + f + ' to ' + folder)

#