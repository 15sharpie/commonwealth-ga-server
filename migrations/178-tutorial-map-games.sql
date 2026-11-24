-- 178: register the four intro (tutorial) missions in map_game_info.
--
-- Each map's TgSeqAct_RequestMission names the next mission, read from the
-- shipped map packages:
--   Inception_3_TEMP (streamed by Inception_ALL) -> 1264
--   Adrenaline_P -> 1271    Skylark_P -> 1281    AgencyZero_P -> 1175 (DomeCity_P_VER_3)
-- The ids come from asm_data_set_production_map_game_list's four entries that
-- had no map_game_info row (1108, 1264, 1271, 1281). 1281 is AgencyZero: its
-- game_used_resources are the only ones with Gruff, the Elite Helot and the flak
-- turrets, and AgencyZero_P is the only intro map that names them. That fixes the
-- chain 1108 Inception -> 1264 Adrenaline -> 1271 Skylark -> 1281 AgencyZero.
--
-- Gameplay type 1552 = "PVE- Solo Mission". Loading screens: Skylark 7549 and
-- AgencyZero 7550 (HUD_MissionLoads.PvE_CP.*); Inception and Adrenaline have none
-- in the data and keep the fallback GO_PLAY already used (5716). Names: 25162
-- "Inception", 31999 "Introduction II", 32144 "Introduction III", 27838 "Agency
-- Zero Headquarters". One-hour mission timer, no overtime.

INSERT INTO map_game_info
    (map_game_id, map_name, game_class, gameplay_type_value_id, friendly_name_msg_id,
     entry_background_image_res_id, mission_time_secs, is_pvp, overtime_secs,
     allow_overtime, share_home_core)
SELECT t.map_game_id, t.map_name, 'TgGame.TgGame_Mission', 1552, t.name_msg,
       t.loading_res, 3600, 0, 0, 0, 0
FROM (
    SELECT 1108 AS map_game_id, 'Inception_ALL' AS map_name, 25162 AS name_msg, 5716 AS loading_res UNION ALL
    SELECT 1264, 'Adrenaline_P',  31999, 5716 UNION ALL
    SELECT 1271, 'Skylark_P',     32144, 7549 UNION ALL
    SELECT 1281, 'AgencyZero_P',  27838, 7550
) AS t
WHERE NOT EXISTS (SELECT 1 FROM map_game_info m WHERE m.map_game_id = t.map_game_id);
