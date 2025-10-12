module Levels
  LEVEL_DATA = [
    {
      name: 'Grassy Hills',
      target_score: 10,
      terrain_points: [
        { x: 1280, y: 180 }, { x: 1000, y: 200 }, { x: 800, y: 160 },
        { x: 600, y: 180 }, { x: 400, y: 120 }, { x: 200, y: 150 },
        { x: 0, y: 100 }
      ],
      scan_area: { x: 200, y: 100, w: 880, h: 400 }
    },
    {
      name: 'Jagged Peaks',
      target_score: 20,
      terrain_points: [
        { x: 1280, y: 150 }, { x: 1100, y: 350 }, { x: 950, y: 200 },
        { x: 800, y: 400 }, { x: 640, y: 250 }, { x: 450, y: 300 },
        { x: 250, y: 180 }, { x: 0, y: 120 }
      ],
      scan_area: { x: 150, y: 100, w: 980, h: 500 }
    }
    # Add more levels here...
  ].freeze

  def self.get(level_index)
    LEVEL_DATA[level_index]
  end
end