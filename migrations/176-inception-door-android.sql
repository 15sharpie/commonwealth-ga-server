-- 176: Inception_ALL (first tutorial map) -- the android at the first door.
--
-- The map's kismet switches bot factory 11812 on ("Spawn malfunc bot") and
-- opens the door from that factory's TgSeqEvent_BotDied ("stationary android
-- dead"). The factory carries no spawn table and Inception has no
-- map_object_config rows, so it spawned nothing and new players were stuck at
-- the door. Bot 1337 (Malfunctioning Android, bot type _Tutorial) is in no
-- stock spawn table, so it gets a one-row mod table at Low Security (1028), the
-- difficulty tutorial maps run at.
--
-- Task force / team 2 is the enemy side: tutorial players arrive on task force 1.
-- No respawn: the door opens on the first death.

INSERT INTO mod_data_set_bot_spawn_tables
    (bot_spawn_table_id, difficulty_value_id, player_profile_id, spawn_group,
     enemy_bot_id, bot_count, spawn_chance, team_size, multiple_class_flag,
     bbm_hp, spawn_group_min, spawn_group_max, spawn_group_respawn_sec, bbm_dmg)
SELECT 10014, 1028, 0, 1, 1337, 1, 1.0, 0, 0, 1.0, 0, 0, 0, 0.0
WHERE NOT EXISTS (SELECT 1 FROM mod_data_set_bot_spawn_tables WHERE bot_spawn_table_id = 10014);

INSERT INTO map_object_config (map_object_id, column_name, value, map_name)
SELECT 11812, c.column_name, c.value, 'Inception_ALL'
FROM (
    SELECT 's_n_task_force'           AS column_name, '2'     AS value UNION ALL
    SELECT 's_n_team_number',                         '2'              UNION ALL
    SELECT 'n_default_spawn_table_id',                '10014'          UNION ALL
    SELECT 'n_spawn_table_id',                        '10014'          UNION ALL
    SELECT 'b_respawn',                               '0'
) AS c
WHERE NOT EXISTS (
    SELECT 1 FROM map_object_config m
    WHERE m.map_name = 'Inception_ALL' AND m.map_object_id = 11812
      AND m.column_name = c.column_name
);
