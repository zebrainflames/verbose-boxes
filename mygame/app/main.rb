module PhysicsHelpers
  # Updated to accept allow_sleep
  def create_body(args, type, x, y, allow_sleep: true)
    args.state.world.create_body(type, x, y, allow_sleep)
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
      'violet' => [200, 160, 220, 120],
      'yellow' => [253, 253, 150, 120],
      'orange' => [255, 204, 153, 120],
      'blue'   => [173, 216, 230, 120],
      'red'    => [255, 153, 153, 120],
      'green'  => [153, 255, 153, 120]
    }
    @active_block = nil
  end

  def setup
    args.state.world = FFI::Box2D::World.new(args.grid.w, args.grid.h)
    args.state.ground = create_static_box(args, args.grid.w / 2, 50, 1280, 10)
    args.state.blocks ||= []
    args.state.paused = false

    # TODO: flag these options somehow
    args.state.profile ||= true
  end

  def tick
    handle_input

    unless args.state.paused
      
      update
    end

    render
  end

  def handle_input
    if args.inputs.keyboard.key_down.p
      args.state.paused = !args.state.paused
    end

    if args.inputs.keyboard.key_down.escape
      $gtk.request_quit
    end

    @active_block = nil if args.inputs.keyboard.key_down.space

    if @active_block
      args.state.horizontal = args.inputs.left_right
      rot_dir = 0.0
      if args.inputs.keyboard.key_down.q
        rot_dir -= 1.0
      end
      if args.inputs.keyboard.key_down.e
        rot_dir += 1.0
      end
      args.state.rot_dir = rot_dir
    end
  end

  def update
    # pre-update: apply impulses / control
    if @active_block
      # TODO: apply impulses
      hor = args.state.horizontal * 10.0
      ver = -2.4 # keep the fall speed smaller than freefall when a block is 'under control'
      @active_block.body.apply_impulse_for_velocity(hor, ver)
    end

    # update Box2D world
    dt = 1.0 / 60.0
    args.state.world.step(dt)

    # Testing: spawn a new random block every 60 ticks
    if (args.state.tick_count % 60) == 0
      spawn_random_tetrimino
      @active_block = args.state.blocks.last
    end

    # post-update
    args.state.blocks.reject! do |block|
      block[:body].position[:y] < -100
    end
  end

  def spawn_random_tetrimino
    n = rand(@block_types.size)
    block_type = @block_types[n]
    color_name = @colors[n]
    spawn_x = args.grid.w / 2 + (rand(100) - 50)
    spawn_y = args.grid.h - 100

    # Create the block, disabling sleep to prevent mid-air freezing
    new_block = send(block_type, args, spawn_x, spawn_y, allow_sleep: true)

    # Set a random rotation
    random_angle = [0, 90, 180, 270].sample
    new_block.angle = random_angle

    args.state.blocks << { body: new_block, color: color_name }
  end

  def render

    sprites = []
    labels = []

    # Render background image
    args.outputs.background_color = [200, 200, 200] 
    sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: 'sprites/ignored/paper_texture2.jpg', a: 180 }

    # Render the ground
    g_pos = args.state.ground.position
    g_ext = args.state.ground.extents
    sprites << {x: g_pos.x, y: g_pos.y, w: g_ext.w, h: g_ext.h, path: "sprites/square/red.png", anchor_x: 0.5, anchor_y: 0.5}

    # Render all composite blocks
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
          path: 'sprites/ignored/tile.png',
          r: tint[0],
          g: tint[1],
          b: tint[2],
          a: tint[3],
          anchor_x: 0.5,
          anchor_y: 0.5,
          angle: body_angle_degrees
        }
      end
    end

    sleeping_blocks = args.state.blocks.size - args.state.blocks.count { |b| b.body.awake? }
    labels << {x: 400.from_right, y: args.grid.h - 10, r: 20, g: 20, b: 20, text: "FPS: #{args.gtk.current_framerate.round} | Blocks: #{args.state.blocks.count} Sleeping blocks: #{sleeping_blocks}"}

    # putz "Some blocks are frozen on tick #{Kernel.tick_count}" if sleeping_blocks > 0

    if args.state.paused
      sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :pixel, r: 0, g: 0, b: 0, a: 150 }
      labels << { x: args.grid.center_x, y: args.grid.center_y, text: "Paused", size_enum: 10, alignment_enum: 1, r: 255, g: 255, b: 255 }
    end

    args.outputs.sprites << sprites
    args.outputs.labels << labels

    if args.state.profile
      args.outputs.primitives << args.gtk.framerate_diagnostics_primitives
      #args.outputs.debug.watch "render debug data here ..."
    end
  end
end

# Main tick loop
def tick(args)
  # One-time setup
  unless args.state.game
    GTK.ffi_misc.gtk_dlopen("ext")
    $game = Game.new(args)
    args.state.game = $game
    $game.setup
  end

  # Calling game logic
  args.state.game.tick
end
