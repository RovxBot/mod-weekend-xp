-- Remove obsolete module text and commands from previous installations.
DELETE FROM `module_string_locale` WHERE `module` = 'mod-weekend-xp';
DELETE FROM `module_string` WHERE `module` = 'mod-weekend-xp';

DELETE FROM `command` WHERE `name` LIKE 'weekendxp%';
DELETE FROM `command` WHERE `name` IN ('weekendbonus status', 'weekendbonus calendar');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('weekendbonus status', 0, 'Syntax: .weekendbonus status\nShows the active weekend bonus and its percentage.'),
('weekendbonus calendar', 0, 'Syntax: .weekendbonus calendar\nShows the next five weekend events in server local time.');
