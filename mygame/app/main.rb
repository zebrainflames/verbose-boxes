module PhysicsHelpers
  # Updated to accept allow_sleep
  def create_body(args, type, x, y, allow_sleep: true)
    args.state.world.create_body(type, x, y, allow_sleep)
  end

  def create_sensor_box(args, x, y, w, h)
    body = create_body(args, "static", x, y, allow_sleep: false)
    body.create_sensor_box(w, h)
    body
  end

  def create_dynamic_box(args, x, y, w, h, density: 1.0, contacts: false, allow_sleep: true)
    body = create_body(args, "dynamic", x, y, allow_sleep: allow_sleep)
    body.create_box_shape(w, h, density, contacts)
    body
  end

  def create_static_box(args, x, y, w, h, density: 1.0, contacts: false)
    body = create_body(args, "static", x, y)
    body.create_box_shape(w, h, density, contacts)
    body
  end

  def create_t_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, "dynamic", x, y, allow_sleep: allow_sleep)
    body.create_t_shape(square_size, density)
    body
  end

  def create_o_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, "dynamic", x, y, allow_sleep: allow_sleep)
    body.create_box_shape_2x2(square_size, density)
    body
  end

  def create_l_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, "dynamic", x, y, allow_sleep: allow_sleep)
    body.create_l_shape(square_size, density)
    body
  end

  def create_j_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, "dynamic", x, y, allow_sleep: allow_sleep)
    body.create_j_shape(square_size, density)
    body
  end
end

class Game
  include PhysicsHelpers
  attr_accessor :active_block
  attr_reader :args, :block_types
  def initialize(args)
    @args = args
    @block_types = [:create_t_block, :create_o_block, :create_l_block, :create_j_block]
    @colors = ['violet', 'orange', 'blue', 'green'] # 'yellow', 'red'
    @pastel_colors = {
      # r, g, b, a
      'violet' => [200, 160, 220],
      'yellow' => [253, 253, 150],
      'orange' => [255, 204, 153],
      'blue'   => [173, 216, 230],
      'red'    => [255, 153, 153],
      'green'  => [153, 255, 153]
    }
    @active_block = nil
    @all_shapes_hit = []
  end

  def setup
    args.state.world = FFI::Box2D::World.new
    
    # Create terrain
    terrain_points = [
      { x: 1280, y: 180 },
      { x: 1000, y: 200 },
      { x: 800, y: 160 },
      { x: 600, y: 180 },
      { x: 400, y: 120 },
      { x: 200, y: 150 },
      { x: 0, y: 100 }
    ]
    
    args.state.ground = create_body(args, "static", 0, 0)
    args.state.ground.create_chain_shape(terrain_points, false)

    args.state.blocks ||= []
    args.state.paused = false


    # rendering setup; RTs etc.
    args.render_target(:static_elements).w = args.grid.w
    args.render_target(:static_elements).h = args.grid.h
    args.render_target(:static_elements).background_color = [0, 0, 0, 0]
    args.render_target(:static_elements).sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: 'sprites/ignored/paper_texture2.jpg', a: 180 }
    ground_shapes = args.state.ground.get_shapes_info
    ground_shapes.each do |shape|
      p1 = { x: shape[:x1], y: shape[:y1] }
      p2 = { x: shape[:x2], y: shape[:y2] }

      angle = Math.atan2(p2.y - p1.y, p2.x - p1.x) * (180 / Math::PI)
      length = Math.sqrt((p2.x - p1.x)**2 + (p2.y - p1.y)**2)

      mx = (p1.x + p2.x) / 2.0
      my = (p1.y + p2.y) / 2.0

      args.render_target(:static_elements).sprites << {
        x: mx,
        y: my,
        w: length,
        h: 12, # Height of the texture
        path: 'sprites/line_test.png',
        angle: angle,
        anchor_x: 0.5,
        anchor_y: 0.5,
        a: 220
      }
    end

    # TODO: flag these options somehow
    args.state.profile ||= false
  end

  def tick
    handle_input

    update unless args.state.paused

    render
  end

  def handle_input
    args.state.paused = !args.state.paused if args.inputs.keyboard.key_down.p

    $gtk.request_quit if args.inputs.keyboard.key_down.escape

    if @active_block
      args.state.horizontal = args.inputs.left_right * 5.0
      args.state.vertical = if args.inputs.up_down < 0.0
                              -10.0
                            else
                              -2.4
                            end
      rot_dir = if args.inputs.keyboard.key_down_or_held? "q"
                  1.0
                elsif args.inputs.keyboard.key_down_or_held? "e"
                  -1.0
                else
                  0.0
                end
      args.state.rot_dir = rot_dir
    end
  end

  def update
    # pre-update: apply impulses / control and update active tetrimino state
    if @active_block
      if @active_block.body.collided?
        # TODO: less magicy numbers, but on  last frame we want to remove horizontal movement...
        @active_block.body.apply_impulse_for_velocity(0.0, -2.4)
        @active_block = nil
      else
        hor = args.state.horizontal
        ver = args.state.vertical
        @active_block.body.apply_impulse_for_velocity(hor, ver)

        # TODO: rotate active block per input
        #unless args.state.rot_dir.zero?
          @active_block.body.rotate(50 * args.state.rot_dir)
        #end
      end
    end

    # update Box2D world
    args.state.world.step

    if (args.state.tick_count % 60) == 0 && @active_block.nil?
      spawn_random_tetrimino
      @active_block = args.state.blocks.last
    end

    check_for_cleared_lines

    # post-update cleanup
    args.state.blocks.reject! do |block|
      block[:body].position[:y] < -100 || block[:body].get_shapes_info.empty?
    end
  end

  def check_for_cleared_lines
    scan_area = { x: 200, y: 100, w: 880, h: 400 }
    num_rays = 20
    min_hits = 6
    vertical_tolerance = 8.0 # TODO: less magic numbers, use block size or something
    horiztonal_tolerance = 1.2 * 32.0

    @raycast_y_coords = []
    @all_shapes_hit = []

    num_rays.times do |i|
      ray_y = scan_area.y + (scan_area.h / num_rays) * i
      @raycast_y_coords << ray_y
      ray_start_x = scan_area.x
      ray_end_x = scan_area.x + scan_area.w

      shapes_hit = args.state.world.raycast(ray_start_x, ray_y, ray_end_x, ray_y, min_hits, vertical_tolerance, horiztonal_tolerance)
      @all_shapes_hit.concat(shapes_hit)
    end
  end

  def spawn_random_tetrimino
    n = rand(@block_types.size)
    block_type = @block_types[n]
    color_name = @colors[n]
    spawn_x = args.grid.w / 2 + (rand(100) - 50)
    spawn_y = args.grid.h - 100

    new_block = send(block_type, args, spawn_x, spawn_y, square_size: 32, allow_sleep: true)

    random_angle = [0, 90, 180, 270].sample
    new_block.angle = random_angle

    args.state.blocks << { body: new_block, color: color_name }
  end

  def render

    sprites = []
    labels = []

    # render the pre-baked background, ground and other baked render targets / static elements
    sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :static_elements }

    # Render all the physical blocks
    # NOTE: this is quite heavy atm and could do with a bunch of optimization
    args.state.blocks.each do |block_info|
      body = block_info[:body]
      color_name = block_info[:color]
      tint = @pastel_colors[color_name]

      body_pos = body.position
      body_angle_degrees = body.angle
      body_angle_rad = body_angle_degrees * (Math::PI / 180.0)

      shapes_info = body.get_shapes_info

      shapes_info.each do |shape|
        rel_x = shape[:x]
        rel_y = shape[:y]

        rotated_rel_x = rel_x * Math.cos(body_angle_rad) - rel_y * Math.sin(body_angle_rad)
        rotated_rel_y = rel_x * Math.sin(body_angle_rad) + rel_y * Math.cos(body_angle_rad)

        final_x = body_pos[:x] + rotated_rel_x
        final_y = body_pos[:y] + rotated_rel_y

        extra_size_px = 2 # we add a tiny bit of extra width and height to the blocks, as the texture has some alpha around the borders
        sprites << {
          x: final_x,
          y: final_y,
          w: shape[:w] + extra_size_px,
          h: shape[:h] + extra_size_px,
          path: 'sprites/tile_crumpled1.png',
          r: tint[0],
          g: tint[1],
          b: tint[2],
          a: 255, # tint[3]
          anchor_x: 0.5,
          anchor_y: 0.5,
          angle: body_angle_degrees
        }
      end
    end

    ### Raycast tests:
    @raycast_y_coords.each do |ray_y|
      args.outputs.lines << {
        x: 200, y: ray_y,
        x2: 200 + 880, y2: ray_y,
        r: 255, g: 100, b: 100, a: 100
      }
    end

    @all_shapes_hit.each do |s|
        sprites << { x: s.x, y: s.y, w: 12, h: 12, path: :pixel, r:153, g:255, b:153, a:255, anchor_x: 0.5, anchor_y: 0.5 }
    end
    ### 

    sleeping_blocks = args.state.blocks.size - args.state.blocks.count { |b| b.body.awake? }
    labels << {alignment_enum: 1, font: "fonts/dirty_harold/dirty_harold.ttf", x: args.grid.w / 2.0, y: args.grid.h - 10, r: 20, g: 20, b: 20, text: "FPS: #{args.gtk.current_framerate.round} | Blocks: #{args.state.blocks.count} Sleeping blocks: #{sleeping_blocks}"}

    labels << {alignment_enum: 1, font: "fonts/dirty_harold/dirty_harold.ttf", x: args.grid.w / 2.0, y: 40.from_top, r: 20, g: 20, b: 20, text: "Active block collided: #{@active_block.body.collided?}"} if @active_block

    if args.state.paused
      sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :pixel, r: 0, g: 0, b: 0, a: 150 }
      labels << { x: args.grid.center_x, y: args.grid.center_y, text: "Paused", size_enum: 10, alignment_enum: 1, r: 255, g: 255, b: 255, font: "fonts/dirty_harold/dirty_harold.ttf" }
    end

    args.outputs.sprites << sprites
    args.outputs.labels << labels

    if args.state.profile
      args.outputs.primitives << args.gtk.framerate_diagnostics_primitives
      #args.outputs.debug.watch "render debug data here ..."
    end
  end
end

def tick(args)
  unless args.state.game
    GTK.ffi_misc.gtk_dlopen("ext")
    $game = Game.new(args)
    args.state.game = $game
    $game.setup
  end

  args.state.game.tick
end

def boot(args)
  args.state = {}
end
