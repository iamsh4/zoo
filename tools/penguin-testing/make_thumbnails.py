import os
from tqdm import tqdm

# Recursively find all png files in the 'reports' folder
png_files = []
for root, dirs, files in os.walk('reports'):
  for file in files:
    if file.endswith('.png'):
      # If it is a thumbnail, skip it
      if file.endswith('-thumb.png'):
        continue
      png_files.append(os.path.join(root, file))

png_files.sort()

# Convert each png file to a thumbnail (thing.png -> thing-thumb.png)
pbar = tqdm(png_files)
for png_file in pbar:
  # Check if the thumbnail already exists
  thumb_file = os.path.splitext(png_file)[0] + '-thumb.png'
  if os.path.exists(thumb_file):
    continue

  # Create the thumbnail at 1/4 the size of the original
  cmd = f'gm convert "{png_file}" -resize 25% "{thumb_file}"'
  pbar.set_description(thumb_file)
  os.system(cmd)
