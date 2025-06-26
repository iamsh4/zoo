import os 

# Before this script is run, run `mogrify -format png */*.ppm` to convert all ppm files to png files

# Create an array of absolute paths to folders under 'reports' directory which contain a 'stdout.txt' file
folders = []
for root, dirs, files in os.walk('reports'):
  if 'stdout.txt' in files:
    folders.append(os.path.abspath(root))

folders.sort()

print("<html>")
print("<head>")
print("<title>Test Results</title>")
print("</head>")
print("<body>")

print("""<style>
  ul.feature-list li {
    font-weight: bold;
    font-size: 15px;
    /* Display like a pill */
    display: inline-block;
    padding: 0.1em 1em;
    margin: 0.1em;
    border-radius: 1em;
    background-color: #f0a0f0;
  }
</style>
""")

for folder in folders:
  # Extract the name of the folder as the name of the game
  game = os.path.basename(folder)

  # 
  print("<hr>")
  print("<div>")
  print(f"<h2>{game}</h2>")

  # read in the lines of the stdout.txt file
  with open(os.path.join(folder, 'stdout.txt')) as f:
    lines = f.readlines()
  
  # Find if 'Playing CD track' is in the lines
  playing_cd = any('Playing CD track' in line for line in lines)
  halted = any('Emulator halted' in line for line in lines)
  halted_unaligned = any('Emulator halted: SH4: CPU instruction read not aligned' in line for line in lines)
  halted_decode_failed = any('Emulator halted: SH4: Tried to generate empty basic block' in line for line in lines)
  halted_unmapped_read = any('Emulator halted: SH4: Table address offset' in line for line in lines)
  uses_opcache = any('CPU MMIO address=0xf6000000' in line for line in lines) or any('CPU MMIO address=0xf7000000' in line for line in lines)

  print('<ul class="feature-list">')
  if playing_cd:
    print("<li>(Plays CD audio)</li>")
  if halted:
    if halted_unaligned:
      print("<li>(Halted unaligned instruction)</li>")
    elif halted_decode_failed:
      print("<li>(Halted decode failed)</li>")
    elif halted_unmapped_read:
      print("<li>(Halted unmapped read)</li>")
    else:
      print("<li>(Halted - unknown reason)</li>")
  if uses_opcache:
    print("<li>(Uses Operand Cache)</li>")
  print("</ul>")

  # Add image for each png in the folder
  thumbs = []
  for file in os.listdir(folder):
    if file.endswith('-thumb.png'):
      thumbs.append(file)
  
  # Sort on the number in the filename
  thumbs = sorted(thumbs, key=lambda x: int(x.split('-')[1]))

  for file in thumbs:
      thumb_fullpath = os.path.join(folder, file)
      fullpath = thumb_fullpath.replace('-thumb.png', '.png')

      # reinterpret these full paths as paths relative to the 'reports' folder
      thumb_fullpath = os.path.relpath(thumb_fullpath, 'reports')
      fullpath = os.path.relpath(fullpath, 'reports')

      print(f'<a href="{fullpath}" target="none"><img src="{thumb_fullpath}"></a>')

  print("</div>")

print("</body>")
print("</html>")