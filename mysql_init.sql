CREATE DATABASE IF NOT EXISTS `ed2kd` DEFAULT CHARACTER SET utf8 COLLATE utf8_unicode_ci;

USE `ed2kd`;

CREATE TABLE IF NOT EXISTS `files` (
  `fid` BINARY(16) NOT NULL COMMENT 'ed2k file hash',
  `name` VARCHAR(128) NOT NULL COMMENT 'file name',
  `ext` VARCHAR(16) NULL COMMENT 'file extension',
  `size` BIGINT UNSIGNED NOT NULL COMMENT 'size in bytes',
  `type` TINYINT UNSIGNED NULL COMMENT 'file type',
  `srcavail` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'available sources',
  `srccomplete` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'complete sources',
  `rating` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'summary rating',
  `rated_count` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'rated sources count',
  `mlength` INT UNSIGNED NULL COMMENT 'media length',
  `mbitrate` INT UNSIGNED NULL COMMENT 'media bitrate',
  `mcodec` VARCHAR(32) NULL COMMENT 'media codec',
  PRIMARY KEY (`fid`),
  UNIQUE INDEX `fid_UIX` (`fid` ASC),
  FULLTEXT INDEX `name_IX` (`name`)
) ENGINE = MyISAM;

CREATE TABLE IF NOT EXISTS `sources` (
  `fid` BINARY(16) NOT NULL COMMENT 'file id',
  `src_id` INT UNSIGNED NOT NULL COMMENT 'source id',
  `src_port` SMALLINT UNSIGNED NOT NULL COMMENT 'source port',
  `complete` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'complete source flag',
  `rating` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'file rating',
  INDEX `fid_IX` (`fid`),
  INDEX `src_IX` (`src_id`,`src_port`)
) ENGINE = MEMORY;


delimiter |


CREATE TRIGGER `sources_ai` AFTER INSERT ON `sources`
  FOR EACH ROW BEGIN
    UPDATE `files` SET
      `srcavail` = `srcavail` + 1,
      `srccomplete` = `srccomplete` + new.complete,
      `rating` = `rating` + new.rating,
      `rated_count` = CASE WHEN new.rating<>0 THEN `rated_count` + 1 ELSE `rated_count` END
    WHERE `fid` = new.fid;
  END;
|

CREATE TRIGGER `sources_bd` BEFORE DELETE ON `sources`
  FOR EACH ROW BEGIN
    UPDATE `files` SET
      `srcavail` = `srcavail` - 1,
      `srccomplete` = `srccomplete` - old.complete,
      `rating` = `rating` - old.rating,
      `rated_count` = CASE WHEN old.rating<>0 THEN `rated_count` - 1 ELSE `rated_count` END
    WHERE `fid` = old.fid;
  END;
|

delimiter ;