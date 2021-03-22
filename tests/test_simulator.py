import random
from copy import copy
from os import path as osp

import magnum as mn
import numpy as np
import pytest

import examples.settings
import habitat_sim


def is_same_state(initial_state, new_state) -> bool:
    same_position = all(initial_state.position == new_state.position)
    same_rotation = np.isclose(initial_state.rotation, new_state.rotation, rtol=1e-4)
    return same_position and same_rotation


def test_no_navmesh_smoke():
    sim_cfg = habitat_sim.SimulatorConfiguration()
    agent_config = habitat_sim.AgentConfiguration()
    # No sensors as we are only testing to see if things work
    # with no navmesh and the navmesh isn't used for any exisitng sensors
    agent_config.sensor_specifications = []

    sim_cfg.scene_id = "data/test_assets/scenes/stage_floor1.glb"
    hab_cfg = habitat_sim.Configuration(sim_cfg, [agent_config])

    mm = habitat_sim.metadata.MetadataMediator(sim_cfg)
    hab_cfg_mm = habitat_sim.Configuration(sim_cfg, [agent_config], mm)

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        with habitat_sim.Simulator(ctor_arg) as sim:
            sim.initialize_agent(0)

            random.seed(0)
            for _ in range(50):
                obs = sim.step(random.choice(list(agent_config.action_space.keys())))
                # Can't collide with no navmesh
                assert not obs["collided"]


def test_empty_scene():
    cfg_settings = examples.settings.default_sim_settings.copy()

    # keyword "NONE" initializes a scene with no scene mesh
    cfg_settings["scene"] = "NONE"
    # test that depth sensor doesn't mind an empty scene
    cfg_settings["depth_sensor"] = True

    hab_cfg = examples.settings.make_cfg(cfg_settings)
    hab_cfg_mm = examples.settings.make_cfg(cfg_settings)
    mm = habitat_sim.metadata.MetadataMediator(hab_cfg.sim_cfg)
    hab_cfg_mm.metadata_mediator = mm

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        with habitat_sim.Simulator(ctor_arg) as sim:
            assert sim.get_stage_initialization_template() == None

            # test that empty frames can be rendered without a scene mesh
            for _ in range(2):
                sim.step(random.choice(list(hab_cfg.agents[0].action_space.keys())))


def test_sim_reset(make_cfg_settings):
    hab_cfg = examples.settings.make_cfg(make_cfg_settings)
    hab_cfg_mm = examples.settings.make_cfg(make_cfg_settings)
    mm = habitat_sim.metadata.MetadataMediator(hab_cfg.sim_cfg)
    hab_cfg_mm.metadata_mediator = mm

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        with habitat_sim.Simulator(ctor_arg) as sim:
            agent_config = sim.config.agents[0]
            sim.initialize_agent(0)
            initial_state = sim.agents[0].initial_state
            # Take random steps in the environment
            for _ in range(10):
                action = random.choice(list(agent_config.action_space.keys()))
                sim.step(action)

            sim.reset()
            new_state = sim.agents[0].get_state()
            same_position = all(initial_state.position == new_state.position)
            same_rotation = np.isclose(
                initial_state.rotation, new_state.rotation, rtol=1e-4
            )  # Numerical error can cause slight deviations
            assert same_position and same_rotation


def test_sim_multiagent_move_and_reset(make_cfg_settings, num_agents=10):
    hab_cfg = examples.settings.make_cfg(make_cfg_settings)
    for agent_id in range(1, num_agents):
        new_agent = copy(hab_cfg.agents[0])
        for sensor_spec in new_agent.sensor_specifications:
            sensor_spec = f"{agent_id}_/{sensor_spec.uuid}"
        hab_cfg.agents.append(new_agent)

    with habitat_sim.Simulator(hab_cfg) as sim:
        agent_initial_states = []
        for i in range(num_agents):
            sim.initialize_agent(i)
            agent_initial_states.append(sim.get_agent(i).state)
        # Take a random multi-agent move
        agent_actions = {}
        for i in range(num_agents):
            agent_config = sim.config.agents[i]
            action = random.choice(list(agent_config.action_space.keys()))
            agent_actions[i] = action
        observations = sim.step(agent_actions)
        # Check all agents either moved or ran into something.
        for initial_state, (agent_id, agent_obs) in zip(
            agent_initial_states, observations.items()
        ):
            assert (
                not is_same_state(initial_state, sim.get_agent(agent_id).state)
                or agent_obs["collided"]
            )
        sim.reset()
        for agent_id, initial_state in enumerate(agent_initial_states):
            # Check all the agents are in their proper position
            assert is_same_state(initial_state, sim.get_agent(agent_id).state)
        del agent_actions[2]
        # Test with one agent being a NOOP. No Sensor Observation will be returned
        observations = sim.step(agent_actions)
        assert is_same_state(
            agent_initial_states[2], sim.get_agent(2).state
        ), "Agent 2 did not move"
        for initial_state, (agent_id, agent_obs) in zip(
            agent_initial_states, observations.items()
        ):
            assert 2 not in observations
            assert agent_id == 2 or (
                not is_same_state(initial_state, sim.get_agent(agent_id).state)
                or agent_obs["collided"]
            )


# Make sure you can keep a reference to an agent alive without crashing
def test_keep_agent():
    sim_cfg = habitat_sim.SimulatorConfiguration()
    agent_config = habitat_sim.AgentConfiguration()

    sim_cfg.scene_id = "data/scene_datasets/habitat-test-scenes/van-gogh-room.glb"
    agents = []
    hab_cfg = habitat_sim.Configuration(sim_cfg, [agent_config])
    mm = habitat_sim.metadata.MetadataMediator(sim_cfg)
    hab_cfg_mm = habitat_sim.Configuration(sim_cfg, [agent_config], mm)

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        for _ in range(3):
            with habitat_sim.Simulator(ctor_arg) as sim:
                agents.append(sim.get_agent(0))


# Make sure you can construct and destruct the simulator multiple times
def test_multiple_construct_destroy():
    sim_cfg = habitat_sim.SimulatorConfiguration()
    agent_config = habitat_sim.AgentConfiguration()

    sim_cfg.scene_id = "data/scene_datasets/habitat-test-scenes/van-gogh-room.glb"
    hab_cfg = habitat_sim.Configuration(sim_cfg, [agent_config])
    mm = habitat_sim.metadata.MetadataMediator(sim_cfg)
    hab_cfg_mm = habitat_sim.Configuration(sim_cfg, [agent_config], mm)

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        for _ in range(3):
            with habitat_sim.Simulator(ctor_arg):
                pass


def test_scene_bounding_boxes():
    cfg_settings = examples.settings.default_sim_settings.copy()
    cfg_settings["scene"] = "data/scene_datasets/habitat-test-scenes/van-gogh-room.glb"
    hab_cfg = examples.settings.make_cfg(cfg_settings)
    hab_cfg_mm = examples.settings.make_cfg(cfg_settings)
    mm = habitat_sim.metadata.MetadataMediator(hab_cfg.sim_cfg)
    hab_cfg_mm.metadata_mediator = mm

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        with habitat_sim.Simulator(ctor_arg) as sim:
            scene_graph = sim.get_active_scene_graph()
            root_node = scene_graph.get_root_node()
            root_node.compute_cumulative_bb()
            scene_bb = root_node.cumulative_bb
            ground_truth = mn.Range3D.from_size(
                mn.Vector3(-0.775869, -0.0233012, -1.6706),
                mn.Vector3(6.76937, 3.86304, 3.5359),
            )
            assert ground_truth == scene_bb


def test_object_template_editing():
    cfg_settings = examples.settings.default_sim_settings.copy()
    cfg_settings["scene"] = "data/scene_datasets/habitat-test-scenes/van-gogh-room.glb"
    cfg_settings["enable_physics"] = True
    hab_cfg = examples.settings.make_cfg(cfg_settings)
    hab_cfg_mm = examples.settings.make_cfg(cfg_settings)
    mm = habitat_sim.metadata.MetadataMediator(hab_cfg.sim_cfg)
    hab_cfg_mm.metadata_mediator = mm

    test_list = [hab_cfg, hab_cfg_mm]
    for ctor_arg in test_list:
        with habitat_sim.Simulator(ctor_arg) as sim:
            # test creating a new template with a test asset
            transform_box_path = osp.abspath(
                "data/test_assets/objects/transform_box.glb"
            )
            transform_box_template = habitat_sim.attributes.ObjectAttributes()
            transform_box_template.render_asset_handle = transform_box_path
            obj_mgr = sim.get_object_template_manager()
            old_library_size = obj_mgr.get_num_templates()
            transform_box_template_id = obj_mgr.register_template(
                transform_box_template, "transform_box_template"
            )
            assert obj_mgr.get_num_templates() > old_library_size
            assert transform_box_template_id != -1

            # test loading a test asset template from file
            sphere_path = osp.abspath("data/test_assets/objects/sphere")
            old_library_size = obj_mgr.get_num_templates()
            template_ids = obj_mgr.load_configs(sphere_path)
            assert len(template_ids) > 0
            assert obj_mgr.get_num_templates() > old_library_size

            # test getting and editing template reference - changes underlying template
            sphere_template = obj_mgr.get_template_by_ID(template_ids[0])
            assert sphere_template.render_asset_handle.endswith("sphere.glb")
            sphere_scale = np.array([2.0, 2.0, 2.0])
            sphere_template.scale = sphere_scale
            obj_mgr.register_template(sphere_template, sphere_template.handle)
            sphere_template2 = obj_mgr.get_template_by_ID(template_ids[0])
            assert sphere_template2.scale == sphere_scale

            # test adding a new object
            object_id = sim.add_object(template_ids[0])
            assert object_id != -1

            # test getting initialization templates
            stage_init_template = sim.get_stage_initialization_template()
            assert stage_init_template.render_asset_handle == cfg_settings["scene"]

            obj_init_template = sim.get_object_initialization_template(object_id)
            assert obj_init_template.render_asset_handle.endswith("sphere.glb")


@pytest.mark.skip(reason="the test requires data not present on the repository")
def test_mesh_point_extraction():
    """Test the interface for retrieving mesh data from instance meshes"""
    cfg_settings = examples.settings.default_sim_settings.copy()
    cfg_settings["scene"] = "office_0/habitat/mesh_semantic.ply"
    hab_cfg = examples.settings.make_cfg(cfg_settings)
    sim = habitat_sim.Simulator(hab_cfg)

    # Try getting a list of mesh IDs
    keys = sim.get_mesh_keys()
    assert len(keys) == 68

    # Test getting information on mesh 0
    vertices = sim.get_vertices(0)
    assert len(vertices) == 13431
    vertices_color = sim.get_vertices_color(0)
    assert len(vertices_color) == 13431
    vertices = sim.get_object_ids(0)
    assert len(vertices) == 13431
    surface_ids = sim.get_surface_ids(0)
    assert len(surface_ids) == 76908


def test_mesh_point_extraction_when_not_instance():
    """Run the mesh extraction methods when the mesh is not
    an instance mesh. This should result in an exception."""
    cfg_settings = examples.settings.default_sim_settings.copy()
    cfg_settings["scene"] = "data/test_assets/scenes/simple_room.glb"
    hab_cfg = examples.settings.make_cfg(cfg_settings)
    sim = habitat_sim.Simulator(hab_cfg)

    # The scene has 6 meshes
    keys = sim.get_mesh_keys()
    assert len(keys) == 7

    # The other mesh data extraction methods should throw an exception
    with pytest.raises(Exception):
        vertices = sim.get_vertices(0)
    with pytest.raises(Exception):
        vertices = sim.get_vertices_color(0)
    with pytest.raises(Exception):
        vertices = sim.get_object_ids(0)
    with pytest.raises(Exception):
        vertices = sim.get_surface_ids(0)
