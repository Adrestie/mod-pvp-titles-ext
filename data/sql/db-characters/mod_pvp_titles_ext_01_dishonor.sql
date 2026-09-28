-- mod-pvp-titles-ext: the Dishonored state (src/PvPTitlesDishonor.cpp)

CREATE TABLE IF NOT EXISTS `mod_pvp_titles_dishonor` (
    `guid`    INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `expires` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'end of the Dishonored state (unix time)',
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-pvp-titles-ext: Dishonored players';

CREATE TABLE IF NOT EXISTS `mod_pvp_titles_dishonorable_kills` (
    `guid` INT UNSIGNED NOT NULL COMMENT 'characters.guid',
    `time` INT UNSIGNED NOT NULL COMMENT 'unix time of the kill',
    KEY `idx_guid_time` (`guid`, `time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-pvp-titles-ext: dishonorable kills within the window';
