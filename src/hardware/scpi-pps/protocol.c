/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdarg.h>
#include "protocol.h"

SR_PRIV int scpi_cmd(const struct sr_dev_inst *sdi, int command, ...)
{
	va_list args;
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	unsigned int i;
	int ret;
	char *cmd;

	devc = sdi->priv;
	cmd = NULL;
	for (i = 0; i < devc->device->num_commands; i++) {
		if (devc->device->commands[i].command == command) {
			cmd = devc->device->commands[i].string;
			break;
		}
	}
	if (!cmd) {
		/* Device does not implement this command, that's OK. */
		return SR_OK_CONTINUE;
	}

	scpi = sdi->conn;
	va_start(args, command);
	ret = sr_scpi_send_variadic(scpi, cmd, args);
	va_end(args);

	return ret;
}

SR_PRIV int scpi_pps_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	const struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	GSList *l;
	float f;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (devc->state == STATE_STOP)
		return TRUE;

	scpi = sdi->conn;

	/* Retrieve requested value for this state. */
	if (sr_scpi_get_float(scpi, NULL, &f) == SR_OK) {
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.channels = g_slist_append(NULL, devc->cur_channel);
		analog.num_samples = 1;
		if (devc->state == STATE_VOLTAGE) {
			analog.mq = SR_MQ_VOLTAGE;
			analog.unit = SR_UNIT_VOLT;
		} else {
			analog.mq = SR_MQ_CURRENT;
			analog.unit = SR_UNIT_AMPERE;
		}
		analog.mqflags = SR_MQFLAG_DC;
		analog.data = &f;
		sr_session_send(sdi, &packet);
		g_slist_free(analog.channels);
	}

	if (devc->state == STATE_VOLTAGE) {
		/* Just got voltage, request current for this channel. */
		devc->state = STATE_CURRENT;
		scpi_cmd(sdi, SCPI_CMD_GET_MEAS_CURRENT, devc->cur_channel->name);
	} else if (devc->state == STATE_CURRENT) {
		/*
		 * Done with voltage and current for this channel, switch to
		 * the next enabled channel.
		 */
		do {
			l = g_slist_find(sdi->channels, devc->cur_channel);
			if (l->next)
				devc->cur_channel = l->next->data;
			else
				devc->cur_channel = sdi->channels->data;
		} while (!devc->cur_channel->enabled);

		/* Request voltage. */
		devc->state = STATE_VOLTAGE;
		scpi_cmd(sdi, SCPI_CMD_GET_MEAS_VOLTAGE, devc->cur_channel->name);
	}

	return TRUE;
}