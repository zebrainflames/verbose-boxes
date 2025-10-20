# generate_pixel_sprite generates a piexel array in the given dimensions to be used as a static sprite in the game later. The pixel array's
# @return (Symbol) the id of the pixel array to be used later
def generate_pixel_sprite args, w, h, background_color = 0xFF000000
  #TODO: use cache counter and keys fields
  $pixel_sprite_ctr ||= 0
  id = "pixel_sprite#{$pixel_sprite_ctr}".to_sym
  $pixel_sprite_ctr += 1
  
  args.pixel_array(id).width = w
  args.pixel_array(id).height = h
  args.pixel_array(id).pixels.fill(background_color, 0, w * h)
  id
end

## Example of usage:
##  args.state.test_circle ||= generate_circle_outline args, 128, 128, 0x0000000
##  args.outputs.sprites << {x: args.grid.w / 2, y: args.grid.h / 2, w: 128, h: 128, path: args.state.test_circle, anchor_x: 0.5, anchor_y: 0.5 }

def generate_circle_outline(args, w, h, background_color = 0xFF000000, color = 0xFF000000)
  args.state.pixel_sprite_cache ||= { sprite_counter: 0, keys: {} }
  key = [w, h, background_color]
  cached = args.state.pixel_sprite_cache[key]
  return cached if cached

  id = generate_pixel_sprite args, w, h, background_color
  # C. Muratori's "Efficient DDA Circle Outline" method, converted to Ruby
  cx, cy = w / 2, h / 2
  r = [w, h].min / 2 - 1
  r = 40
  r2 = r+r
  x = r
  y = 0
  dy = -2
  dx = r2+r2 - 4
  d = r2 - 1

  pixels = args.pixel_array(id).pixels
  while y <= x do
    pixels[((cy - y) * w) + (cx - x)] = color
    pixels[((cy - y) * w) + (cx + x)] = color
    pixels[((cy + y) * w) + (cx - x)] = color
    pixels[((cy + y) * w) + (cx + x)] = color

    pixels[((cy - x) * w) + (cx - y)] = color
    pixels[((cy - x) * w) + (cx + y)] = color
    pixels[((cy + x) * w) + (cx - y)] = color
    pixels[((cy + x) * w) + (cx + y)] = color

    d += dy
    dy -= 4
    y += 1

    mask = d >> 31
    d += dx & mask
    dx -= 4 & mask

    x += mask
  end
  id
end
