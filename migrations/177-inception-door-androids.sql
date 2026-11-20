-- 177: Inception_ALL -- the androids behind Doors 2-5, and one android at door 1.
--
-- Factory 11812 (the door-1 android from 176) ships with bAutoSpawn on, so it
-- spawned once at map load and again when the melee lesson's kismet toggled it
-- on. Kismet-toggled factories elsewhere carry b_auto_spawn=0 (all 246 such
-- rows in the baseline), so 11812 gets the same.
--
-- The map opens Door 2, 3, 4 and 5 from TgSeqEvent_BotDied events whose
-- originators are factories 9832, 9833, 12627 and 12628 (kismet frames
-- "2nd Android" / "3rd Android"). Like 11812 they had no spawn table and
-- spawned nothing, so the next door never opened. They get spawn table 10014
-- (bot 1337, Malfunctioning Android), the enemy task force and no respawn.

INSERT INTO map_object_config (map_object_id, column_name, value, map_name)
SELECT 11812, 'b_auto_spawn', '0', 'Inception_ALL'
WHERE NOT EXISTS (
    SELECT 1 FROM map_object_config
    WHERE map_name = 'Inception_ALL' AND map_object_id = 11812 AND column_name = 'b_auto_spawn'
);

INSERT INTO map_object_config (map_object_id, column_name, value, map_name)
SELECT f.map_object_id, c.column_name, c.value, 'Inception_ALL'
FROM (
    SELECT 9832 AS map_object_id UNION ALL
    SELECT 9833                  UNION ALL
    SELECT 12627                 UNION ALL
    SELECT 12628
) AS f
CROSS JOIN (
    SELECT 's_n_task_force'           AS column_name, '2'     AS value UNION ALL
    SELECT 's_n_team_number',                         '2'              UNION ALL
    SELECT 'n_default_spawn_table_id',                '10014'          UNION ALL
    SELECT 'n_spawn_table_id',                        '10014'          UNION ALL
    SELECT 'b_respawn',                               '0'
) AS c
WHERE NOT EXISTS (
    SELECT 1 FROM map_object_config m
    WHERE m.map_name = 'Inception_ALL' AND m.map_object_id = f.map_object_id
      AND m.column_name = c.column_name
);
