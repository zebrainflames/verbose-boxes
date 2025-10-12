module PhysicsHelpers
  def create_body(args, type, x, y, allow_sleep: true, vx: 0.0, vy: 0.0, angular_velocity: 0.0)
    args.state.world.create_body(type, x, y, allow_sleep, vx, vy, angular_velocity)
  end

  def create_sensor_box(args, x, y, w, h)
    body = create_body(args, 'static', x, y, allow_sleep: false)
    body.create_sensor_box(w, h)
    body
  end

  def create_dynamic_box(args, x, y, w, h, density: 1.0, contacts: false, allow_sleep: true, vx: 0.0, vy: 0.0, angular_velocity: 0.0)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep, vx: vx, vy: vy, angular_velocity: angular_velocity)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.05
    body.create_box_shape(w, h, density, friction, restitution, contacts)
    body
  end

  def create_static_box(args, x, y, w, h, density: 1.0, contacts: false)
    body = create_body(args, 'static', x, y)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_box_shape(w, h, density, friction, restitution, contacts)
    body
  end

  def create_t_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_t_shape(square_size, density, friction, restitution)
    body
  end

  def create_o_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_box_shape_2x2(square_size, density, friction, restitution)
    body
  end

  def create_l_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_l_shape(square_size, density, friction, restitution)
    body
  end

  def create_j_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_j_shape(square_size, density, friction, restitution)
    body
  end

  def create_i_block(args, x, y, square_size: 20, density: 1.0, allow_sleep: true)
    body = create_body(args, 'dynamic', x, y, allow_sleep: allow_sleep)
    friction = args.state.physics&.block_friction || 0.5
    restitution = args.state.physics&.block_restitution || 0.1
    body.create_i_shape(square_size, density, friction, restitution)
    body
  end
end

class Game
  include PhysicsHelpers
  attr_accessor :active_block
  attr_reader :args, :block_types


  # Reset dynamic game state and frame counters to a known baseline
  def start_level(level_index)
    level_data = Levels.get(level_index)
    args.state.world = World.new
    args.state.ground = create_body(args, 'static', 0, 0)
    gf = args.state.physics&.ground_friction || 1.0
    gr = args.state.physics&.ground_restitution || 0.0
    # Create ground chain with explicit material properties; ensure native extension is rebuilt when C changes
    args.state.ground.create_chain_shape(level_data[:terrain_points], false, gf, gr)


    # rendering setup
    unless @static_assets_setup
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
      @static_assets_setup = true
    end

    args.state.current_level_index = level_index
    args.state.blocks = []
    @active_block = nil
    args.state.game_state = :playing

    # Frame/cycle counters and timers
    @spawn_collision_check_frames = 0
    @lock_delay_frames = 8
    @spawn_delay_frames = 45
    @touching_frames = 0
    @pending_spawn_frames = 0 # trigger initial spawn via countdown
    @last_spawned_block = nil
  end

  def reset_game
    args.state.score = 0
    start_level(0)
  end

  def initialize(args)
    @args = args
    @block_types = [:create_t_block, :create_o_block, :create_l_block, :create_j_block, :create_i_block]
    @colors = ['violet', 'orange', 'blue', 'green', 'red', 'yellow'] # 'yellow', 'red'
    @pastel_colors = {
      'violet' => [200, 160, 220],
      'yellow' => [253, 253, 150],
      'orange' => [255, 204, 153],
      'blue'   => [173, 216, 230],
      'red'    => [255, 153, 153],
      'green'  => [153, 255, 153]
    }
    @active_block = nil
    @all_shapes_hit = []
    @all_raycast_hits = []
    @debug_colors = [
      [255, 0, 0], [0, 255, 0], [0, 0, 255], [255, 255, 0], [0, 255, 255],
      [255, 0, 255], [192, 192, 192], [128, 128, 128], [128, 0, 0], [128, 128, 0],
      [0, 128, 0], [128, 0, 128], [0, 128, 128], [0, 0, 128], [255, 165, 0],
      [255, 215, 0], [184, 134, 11], [218, 165, 32], [255, 250, 205], [255, 228, 181]
    ]
  end

  def setup
    # physics tunables (Ruby-side) for quick iteration on gameplay feel
    args.state.physics ||= { block_friction: 0.8, block_restitution: 0.05, ground_friction: 1.0, ground_restitution: 0.0 }

    args.state.blocks ||= []
    args.state.profile ||= false

    reset_game
  end

  def tick
    handle_input

    case args.state.game_state
    when :playing
      update
    when :paused
      # paused: no update
    when :game_over
      # Game over: no update
    end

    render
  end

  def handle_input
    $gtk.request_quit if args.inputs.keyboard.key_down.escape # TODO: check what this does on mobile & Web

    # Quick reset any time for rapid tuning
    if args.inputs.keyboard.key_down.r
      reset_game
      return
    end

    # Physics tuning controls
    tune_physics_params

    if args.state.game_state == :game_over
      if args.inputs.keyboard.key_down.space
        reset_game
      end
      return
    end

    if args.inputs.keyboard.key_down.p
      if args.state.game_state == :paused
        args.state.game_state = :playing
      elsif args.state.game_state == :playing
        args.state.game_state = :paused
      end
    end

    if @active_block
      args.state.horizontal = args.inputs.left_right * 5.0
      args.state.vertical = if args.inputs.up_down < 0.0
                              -10.0
                            else
                              -2.4
                            end
      rot_dir = if args.inputs.keyboard.key_down_or_held? 'q'
                  1.0
                elsif args.inputs.keyboard.key_down_or_held? 'e'
                  -1.0
                else
                  0.0
                end
      args.state.rot_dir = rot_dir * 50.0
    end
  end

  def tune_physics_params
    # friction: - and +
    if args.inputs.keyboard.key_down? :hyphen
      f = args.state.physics.block_friction - 0.05
      args.state.physics.block_friction = f < 0.0 ? 0.0 : (f > 2.0 ? 2.0 : f)
    elsif args.inputs.keyboard.key_down? :plus
      f = args.state.physics.block_friction + 0.05
      args.state.physics.block_friction = f < 0.0 ? 0.0 : (f > 2.0 ? 2.0 : f)
    end

    # restitution: , and .
    if args.inputs.keyboard.key_down? :comma
      r = args.state.physics.block_restitution - 0.05
      args.state.physics.block_restitution = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r)
    elsif args.inputs.keyboard.key_down? :period
      r = args.state.physics.block_restitution + 0.05
      args.state.physics.block_restitution = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r)
    end
  end

  def update
    # pre-update: apply impulses / control and update active tetrimino state
    if @active_block
      hor = args.state.horizontal
      ver = args.state.vertical
      @active_block.body.apply_impulse_for_velocity(hor, ver)
      @active_block.body.rotate(args.state.rot_dir)
    end

    # update Box2D world
    args.state.world.step

    # Post-step: handle lock delay for active block collisions, spawn delay etc.
    if @active_block
      if @active_block.body.collided?
        @touching_frames += 1
        if @touching_frames >= @lock_delay_frames
          @active_block.body.apply_impulse_for_velocity(0.0, -2.4)
          @active_block = nil
          @touching_frames = 0
          @pending_spawn_frames = @spawn_delay_frames
        end
      else
        @touching_frames = 0
      end
    end

    # Handle pending spawn delay
    if @active_block.nil? && @pending_spawn_frames
      @pending_spawn_frames -= 1
      if @pending_spawn_frames <= 0
        spawn_random_tetrimino
        @active_block = args.state.blocks.last
        @last_spawned_block = @active_block
        @spawn_collision_check_frames = 2
        @pending_spawn_frames = nil
      end
    end

    # post-physics step: check for immediate collision of just spawned block
    if @spawn_collision_check_frames > 0 && @last_spawned_block
      if @last_spawned_block.body.collided?
        putz "Last spawned block immediatelly collided!"
        args.state.game_state = :game_over
      end
      @spawn_collision_check_frames -= 1
      @last_spawned_block = nil if @spawn_collision_check_frames <= 0
    end


    # scoring mechanics
    check_for_cleared_lines

    # post-update cleanup
    # NOTE: this is a separate block instead of combined mainly to track if this condition happens in game
    args.state.blocks.reject! do |block|
      empty = block.body.get_shapes_info.empty?
      puts "Block with no shapes removed!" if empty
      empty
    end
    # NOTE: this could affect scoring as well? Minus points on blocks "lost" ?
    args.state.blocks.reject! do |block|
      block.body.position.y < -100 
    end
  end

  def split_body(block_info)
    original_body = block_info[:body]
    info = original_body.get_info
    remaining_shapes = original_body.get_shapes_info

    if remaining_shapes.any?
      angle_rad = info[:angle] * (Math::PI / 180.0)
      cos_a = Math.cos(angle_rad)
      sin_a = Math.sin(angle_rad)

      remaining_shapes.each do |shape|
        # Calculate world position of the new body from the shape's relative position
        rotated_x = shape[:x] * cos_a - shape[:y] * sin_a
        rotated_y = shape[:x] * sin_a + shape[:y] * cos_a
        new_x = info[:x] + rotated_x
        new_y = info[:y] + rotated_y

        # Create a new body for the shape, passing initial velocities
        new_body = create_dynamic_box(args, new_x, new_y, shape[:w], shape[:h],
                                      vx: info[:vx], vy: info[:vy], angular_velocity: info[:angular_velocity])

        # Add to game state
        args.state.blocks << { body: new_body, color: block_info[:color] }      end
    end

    # Destroy original body and remove from game state
    original_body.destroy
    args.state.blocks.delete(block_info)
  end

  def check_for_cleared_lines
    level_data = Levels.get(args.state.current_level_index)
    level_scan = level_data[:scan_area]

    # Determine vertical scan range dynamically: from terrain bottom to spawn height (top)
    terrain_bottom = level_data[:terrain_points].map { |p| p[:y] || p["y"] }.min
    spawn_top = args.grid.h - 100
    scan_y = terrain_bottom
    scan_h = [spawn_top - scan_y, 1].max

    # Use level-provided x-range
    scan_x = level_scan[:x]
    scan_w = level_scan[:w]

    num_rays = 20
    min_hits = level_data[:line_min_blocks] || 6
    vertical_tolerance = 10.0 # TODO: less magic numbers, use block size or something
    horiztonal_tolerance = 1.4 * 32.0

    @raycast_y_coords = []
    @cleared_shape_origins = []
    @all_raycast_hits = []

    total_bodies_to_split = []

    num_rays.times do |i|
      ray_y = scan_y + (scan_h / num_rays) * i
      @raycast_y_coords << ray_y
      ray_start_x = scan_x
      ray_end_x = scan_x + scan_w

      raycast_results = args.state.world.raycast(ray_start_x, ray_y, ray_end_x, ray_y, min_hits, vertical_tolerance, horiztonal_tolerance)
      next if raycast_results.empty?

      @cleared_shape_origins.concat raycast_results.cleared_points
      args.state.score += raycast_results.cleared_points.length
      total_bodies_to_split.concat raycast_results.bodies_to_split

      if args.state.profile
        color_index = i % @debug_colors.size
        @all_raycast_hits << { points: raycast_results.all_hits, color_index: color_index } 
      end
    end

    total_bodies_to_split.uniq! # Process each affected body only once

    total_bodies_to_split.each do |body_to_split|
      block_info = args.state.blocks.find { |b| b[:body] == body_to_split }
      split_body(block_info) if block_info
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

    sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :static_elements }

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

        extra_size_px = 1 # a tiny bit of extra width and height to the blocks, as the texture has some buffer
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

    @all_raycast_hits.each do |hit_group|
      color = @debug_colors[hit_group[:color_index]]
      hit_group[:points].each do |p|
        sprites << { x: p.x, y: p.y, w: 5, h: 5, path: :pixel, r: color[0], g: color[1], b: color[2], a: 200, anchor_x: 0.5, anchor_y: 0.5 }
      end
    end

    @cleared_shape_origins.each do |s|
      sprites << { x: s.x, y: s.y, w: 12, h: 12, path: :pixel, r:153, g:255, b:153, a:255, anchor_x: 0.5, anchor_y: 0.5 }
    end

    labels << { alignment_enum: 1, font: 'fonts/dirty_harold/dirty_harold.ttf', x: args.grid.w / 2.0,
               y: args.grid.h - 10, r: 20, g: 20, b: 20, 
               text: "FPS: #{args.gtk.current_framerate.round} | Score: #{args.state.score}" }


    if args.state.game_state == :paused
      sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :pixel, r: 0, g: 0, b: 0, a: 150 }
      labels << { x: args.grid.center_x, y: args.grid.center_y, text: 'Paused', size_enum: 10,
                  alignment_enum: 1, r: 255, g: 255, b: 255, font: 'fonts/dirty_harold/dirty_harold.ttf' }
    end

    if args.state.game_state == :game_over
      sprites << { x: 0, y: 0, w: args.grid.w, h: args.grid.h, path: :pixel, r: 0, g: 0, b: 0, a: 180 }
      labels << { x: args.grid.center_x, y: args.grid.center_y + 40, text: 'Game Over', size_enum: 10,
                  alignment_enum: 1, r: 255, g: 255, b: 255, font: 'fonts/dirty_harold/dirty_harold.ttf' }
      labels << { x: args.grid.center_x, y: args.grid.center_y - 10, text: "Final Score: #{args.state.score}", size_enum: 4,
                  alignment_enum: 1, r: 240, g: 240, b: 240, font: 'fonts/dirty_harold/dirty_harold.ttf' }
      labels << { x: args.grid.center_x, y: args.grid.center_y - 60, text: 'Press R to restart', size_enum: 4,
                  alignment_enum: 1, r: 240, g: 240, b: 240, font: 'fonts/dirty_harold/dirty_harold.ttf' }
    end

    if args.state.profile
      args.outputs.primitives << args.gtk.framerate_diagnostics_primitives


      # Also show physics tunables alongside profile data
      pf = args.state.physics.block_friction.round(3)
      pr = args.state.physics.block_restitution.round(3)
      labels << { x: 120.from_right, y: args.grid.h - 10, text: "Friction: #{pf}", size_enum: 2, r: 60, g: 60, b: 60, font: 'fonts/dirty_harold/dirty_harold.ttf' }
      labels << { x: 120.from_right, y: args.grid.h - 30, text: "Restitution: #{pr}", size_enum: 2, r: 60, g: 60, b: 60, font: 'fonts/dirty_harold/dirty_harold.ttf' }

      @raycast_y_coords.each do |ray_y|
        args.outputs.lines << {
          x: level_data[:scan_area][:x], y: ray_y,
          x2: level_data[:scan_area][:x] + level_data[:scan_area][:w], y2: ray_y,
          r: 255, g: 100, b: 100, a: 100
        }
      end
    end

    args.outputs.sprites << sprites
    args.outputs.labels << labels
  end
end

def tick(args)
  unless args.state.game
    GTK.ffi_misc.gtk_dlopen('ext')
    require 'app/levels.rb'
    include FFI::Box2D
    $game = Game.new(args)
    args.state.game = $game
    $game.setup
  end

  args.state.game.tick
end

def boot(args)
  args.state = {}
end
