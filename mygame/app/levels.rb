module Levels
  LEVEL_DATA = [
    {
      name: 'Bumpy Flats',
      target_score: 10,
      line_min_blocks: 10,
      terrain_points: [
        # Clockwise chain with visible vertical walls at x=200 and x=1080 (leaves UI margins on both sides)
        # Start above the screen at the top-right, go down the right wall, traverse ground right->left across bumps, then up the left wall
        { x: 1080, y: 1020 },
        { x: 1080, y: 920 },
        { x: 1080, y: 620 },
        { x: 1080, y: 96 },
        # Ground traversal from right to left (positions and heights snapped to 32px grid)
        { x: 1056, y: 96 }, { x: 1056, y: 160 }, { x: 960, y: 160 }, { x: 960, y: 96 },
        { x: 800, y: 96 },  { x: 800, y: 192 }, { x: 672, y: 192 }, { x: 672, y: 96 },
        { x: 544, y: 96 },  { x: 544, y: 160 }, { x: 480, y: 160 }, { x: 480, y: 96 },
        { x: 352, y: 96 },  { x: 352, y: 224 }, { x: 288, y: 224 }, { x: 288, y: 96 },
        { x: 200, y: 96 },
        { x: 200, y: 820 },
        { x: 200, y: 920 }
      ],
      scan_area: { x: 200, y: 96, w: 880, h: 400 }
    },
    {
      name: 'Jagged Peaks',
      target_score: 20,
      line_min_blocks: 6,
      terrain_points: [
        # Clockwise chain with visible vertical walls at x=200 and x=1080 (leaves UI margins on both sides)
        # Start above top-right, descend right wall, traverse jagged terrain right->left, then ascend left wall
        { x: 1080, y: 820 },
        { x: 1080, y: 150 },
        { x: 1000, y: 350 }, { x: 850, y: 200 }, { x: 700, y: 400 },
        { x: 540, y: 250 }, { x: 450, y: 300 }, { x: 300, y: 180 },
        { x: 200, y: 100 },
        { x: 200, y: 820 }
      ],
      scan_area: { x: 200, y: 100, w: 880, h: 500 }
    }
    # Add more levels here...
  ].freeze

  def self.get(level_index)
    LEVEL_DATA[level_index]
  end
end