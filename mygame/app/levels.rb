module Levels
  EDGE_LEFT = 350
  EDGE_RIGHT = EDGE_LEFT.from_right
  LEVEL_DATA = [
    {
      name: 'Bumpy Flats',
      target_score: 10,
      line_min_blocks: 10,
      terrain_points: [
        # Testing general settings for levels -- 
        # walls at 360 pixels from edges, ground width 560 pixels, ground height
        # 100 pixels
        # right wall:
        { x: EDGE_RIGHT + 80, y: 1000 },
        { x: EDGE_RIGHT + 60, y: 820 },
        { x: EDGE_RIGHT + 20, y: 520 },
        { x: EDGE_RIGHT, y: 100 },
        # 'ground'
        { x: 380.from_right, y: 100 }, 
        { x: 380.from_right, y: 120 },
        { x: 420.from_right, y: 120 },
        { x: 420.from_right, y: 100 },
        { x: 520, y: 100 },
        { x: 520, y: 140 },
        { x: 480, y: 140 },
        { x: 400, y: 160 },
        { x: 380, y: 160 },
        # left wall
        { x: EDGE_LEFT, y: 140 },
        { x: EDGE_LEFT - 20, y: 520 },
        { x: EDGE_LEFT - 60, y: 820 },
        { x: EDGE_LEFT - 80, y: 1000 }
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
