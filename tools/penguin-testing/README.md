# Compatibility Testing Steps
0. Ensure everything is built.
1. Create a 'reports' folder in the root of the repository.
1. `make_report.py` should be run from the root of the repository. It will run all the tests
    and output data to a reports folder. Each disc executed will have its own folder in the
    reports folder and contain a stdout.txt as well as PPM screenshots of the test.
2. Run `mogrify -format png reports/*/*.ppm` to create PNGs for all PPM files.
3. Run `python3 make_thumbnails.py` from the root of the repository to create thumbnails for all PNG files.
4. Run `python3 make_html.py` from the root of the repository to create an HTML file with embedded images and other data extracted from the stdout.txt files.
