# Creating icons with rounded rectangles
```
convert -size 256x256 xc:none -draw "roundrectangle 0,0,256,256,32,32" mask.png
convert input_image.png -matte mask.png -compose DstIn -composite output.bmp
```

Why BMP? SDL supports it without an extra library. 
