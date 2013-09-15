/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2009-2010 Daniel De Graaf <danieldg@inspircd.org>
 *   Copyright (C) 2008-2009 Robin Burchell <robin+git@viroteck.net>
 *
 * This file is part of InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "inspircd.h"

/* $ModDesc: Provides support for channel mode +P to provide permanent channels */

struct ListModeData
{
	std::string modes;
	std::string params;
};

// Not in a class due to circular dependancy hell.
static std::string permchannelsconf;
static bool WriteDatabase(Module* mod, bool save_listmodes)
{
	FILE *f;

	if (permchannelsconf.empty())
	{
		// Fake success.
		return true;
	}

	std::string tempname = permchannelsconf + ".tmp";

	/*
	 * We need to perform an atomic write so as not to fuck things up.
	 * So, let's write to a temporary file, flush and sync the FD, then rename the file..
	 *		-- w00t
	 */
	f = fopen(tempname.c_str(), "w");
	if (!f)
	{
		ServerInstance->Logs->Log("m_permchannels",DEFAULT, "permchannels: Cannot create database! %s (%d)", strerror(errno), errno);
		ServerInstance->SNO->WriteToSnoMask('a', "database: cannot create new db: %s (%d)", strerror(errno), errno);
		return false;
	}

	fputs("# Permchannels DB\n# This file is autogenerated; any changes will be overwritten!\n<config format=\"compat\">\n", f);
	// Now, let's write.
	std::string line;
	for (chan_hash::const_iterator i = ServerInstance->chanlist->begin(); i != ServerInstance->chanlist->end(); i++)
	{
		Channel* chan = i->second;
		if (!chan->IsModeSet('P'))
			continue;

		std::string chanmodes = chan->ChanModes(true);
		if (save_listmodes)
		{
			ListModeData lm;

			// Bans are managed by the core, so we have to process them separately
			lm.modes = std::string(chan->bans.size(), 'b');
			for (BanList::const_iterator j = chan->bans.begin(); j != chan->bans.end(); ++j)
			{
				lm.params += j->data;
				lm.params += ' ';
			}

			// All other listmodes are managed by modules, so we need to ask them (call their
			// OnSyncChannel() handler) to give our ProtoSendMode() a list of modes that are
			// set on the channel. The ListModeData struct is passed as an opaque pointer
			// that will be passed back to us by the module handling the mode.
			FOREACH_MOD(I_OnSyncChannel, OnSyncChannel(chan, mod, &lm));

			if (!lm.modes.empty())
			{
				// Remove the last space
				lm.params.erase(lm.params.end()-1);

				// If there is at least a space in chanmodes (that is, a non-listmode has a parameter)
				// insert the listmode mode letters before the space. Otherwise just append them.
				std::string::size_type p = chanmodes.find(' ');
				if (p == std::string::npos)
					chanmodes += lm.modes;
				else
					chanmodes.insert(p, lm.modes);

				// Append the listmode parameters (the masks themselves)
				chanmodes += ' ';
				chanmodes += lm.params;
			}
		}

		std::string chants = ConvToStr(chan->age);
		std::string topicts = ConvToStr(chan->topicset);
		const char* items[] =
		{
			"<permchannels channel=",
			chan->name.c_str(),
			" ts=",
			chants.c_str(),
			" topic=",
			chan->topic.c_str(),
			" topicts=",
			topicts.c_str(),
			" topicsetby=",
			chan->setby.c_str(),
			" modes=",
			chanmodes.c_str(),
			">\n"
		};

		line.clear();
		int item = 0, ipos = 0;
		while (item < 13)
		{
			char c = items[item][ipos++];
			if (c == 0)
			{
				// end of this string; hop to next string, insert a quote
				item++;
				ipos = 0;
				c = '"';
			}
			else if (c == '\\' || c == '"')
			{
				line += '\\';
			}
			line += c;
		}

		// Erase last '"'
		line.erase(line.end()-1);
		fputs(line.c_str(), f);
	}

	int write_error = 0;
	write_error = ferror(f);
	write_error |= fclose(f);
	if (write_error)
	{
		ServerInstance->Logs->Log("m_permchannels",DEFAULT, "permchannels: Cannot write to new database! %s (%d)", strerror(errno), errno);
		ServerInstance->SNO->WriteToSnoMask('a', "database: cannot write to new db: %s (%d)", strerror(errno), errno);
		return false;
	}

#ifdef _WIN32
	if (remove(permchannelsconf.c_str()))
	{
		ServerInstance->Logs->Log("m_permchannels",DEFAULT, "permchannels: Cannot remove old database! %s (%d)", strerror(errno), errno);
		ServerInstance->SNO->WriteToSnoMask('a', "database: cannot remove old database: %s (%d)", strerror(errno), errno);
		return false;
	}
#endif
	// Use rename to move temporary to new db - this is guarenteed not to fuck up, even in case of a crash.
	if (rename(tempname.c_str(), permchannelsconf.c_str()) < 0)
	{
		ServerInstance->Logs->Log("m_permchannels",DEFAULT, "permchannels: Cannot move new to old database! %s (%d)", strerror(errno), errno);
		ServerInstance->SNO->WriteToSnoMask('a', "database: cannot replace old with new db: %s (%d)", strerror(errno), errno);
		return false;
	}

	return true;
}



/** Handles the +P channel mode
 */
class PermChannel : public ModeHandler
{
 public:
	PermChannel(Module* Creator) : ModeHandler(Creator, "permanent", 'P', PARAM_NONE, MODETYPE_CHANNEL) { oper = true; }

	ModeAction OnModeChange(User* source, User* dest, Channel* channel, std::string &parameter, bool adding)
	{
		if (adding)
		{
			if (!channel->IsModeSet('P'))
			{
				channel->SetMode('P',true);
				return MODEACTION_ALLOW;
			}
		}
		else
		{
			if (channel->IsModeSet('P'))
			{
				channel->SetMode(this,false);
				if (channel->GetUserCounter() == 0)
				{
					channel->DelUser(ServerInstance->FakeClient);
				}
				return MODEACTION_ALLOW;
			}
		}

		return MODEACTION_DENY;
	}
};

class ModulePermanentChannels : public Module
{
	PermChannel p;
	bool dirty;
	bool loaded;
	bool save_listmodes;
public:

	ModulePermanentChannels()
		: p(this), dirty(false), loaded(false)
	{
	}

	void init()
	{
		ServerInstance->Modules->AddService(p);
		Implementation eventlist[] = { I_OnChannelPreDelete, I_OnPostTopicChange, I_OnRawMode, I_OnRehash, I_OnBackgroundTimer };
		ServerInstance->Modules->Attach(eventlist, this, sizeof(eventlist)/sizeof(Implementation));

		OnRehash(NULL);
	}

	CullResult cull()
	{
		/*
		 * DelMode can't remove the +P mode on empty channels, or it will break
		 * merging modes with remote servers. Remove the empty channels now as
		 * we know this is not the case.
		 */
		chan_hash::iterator iter = ServerInstance->chanlist->begin();
		while (iter != ServerInstance->chanlist->end())
		{
			Channel* c = iter->second;
			if (c->GetUserCounter() == 0)
			{
				chan_hash::iterator at = iter;
				iter++;
				FOREACH_MOD(I_OnChannelDelete, OnChannelDelete(c));
				ServerInstance->chanlist->erase(at);
				ServerInstance->GlobalCulls.AddItem(c);
			}
			else
				iter++;
		}
		ServerInstance->Modes->DelMode(&p);
		return Module::cull();
	}

	virtual void OnRehash(User *user)
	{
		ConfigTag* tag = ServerInstance->Config->ConfValue("permchanneldb");
		permchannelsconf = tag->getString("filename");
		save_listmodes = tag->getBool("listmodes");
	}

	void LoadDatabase()
	{
		/*
		 * Process config-defined list of permanent channels.
		 * -- w00t
		 */
		ConfigTagList permchannels = ServerInstance->Config->ConfTags("permchannels");
		for (ConfigIter i = permchannels.first; i != permchannels.second; ++i)
		{
			ConfigTag* tag = i->second;
			std::string channel = tag->getString("channel");
			std::string topic = tag->getString("topic");
			std::string modes = tag->getString("modes");

			if ((channel.empty()) || (channel.length() > ServerInstance->Config->Limits.ChanMax))
			{
				ServerInstance->Logs->Log("m_permchannels", DEFAULT, "Ignoring permchannels tag with empty or too long channel name (\"" + channel + "\")");
				continue;
			}

			Channel *c = ServerInstance->FindChan(channel);

			if (!c)
			{
				time_t TS = tag->getInt("ts");
				c = new Channel(channel, ((TS > 0) ? TS : ServerInstance->Time()));

				c->SetTopic(NULL, topic, true);
				c->setby = tag->getString("topicsetby");
				if (c->setby.empty())
					c->setby = ServerInstance->Config->ServerName;
				unsigned int topicset = tag->getInt("topicts");
				// SetTopic() sets the topic TS to now, if there was no topicts saved then don't overwrite that with a 0
				if (topicset > 0)
					c->topicset = topicset;

				ServerInstance->Logs->Log("m_permchannels", DEBUG, "Added %s with topic %s", channel.c_str(), topic.c_str());

				if (modes.empty())
					continue;

				irc::spacesepstream list(modes);
				std::string modeseq;
				std::string par;

				list.GetToken(modeseq);

				// XXX bleh, should we pass this to the mode parser instead? ugly. --w00t
				for (std::string::iterator n = modeseq.begin(); n != modeseq.end(); ++n)
				{
					ModeHandler* mode = ServerInstance->Modes->FindMode(*n, MODETYPE_CHANNEL);
					if (mode)
					{
						if (mode->GetNumParams(true))
							list.GetToken(par);
						else
							par.clear();

						mode->OnModeChange(ServerInstance->FakeClient, ServerInstance->FakeClient, c, par, true);
					}
				}
			}
		}
	}

	virtual ModResult OnRawMode(User* user, Channel* chan, const char mode, const std::string &param, bool adding, int pcnt)
	{
		if (chan && (chan->IsModeSet('P') || mode == 'P'))
			dirty = true;

		return MOD_RES_PASSTHRU;
	}

	virtual void OnPostTopicChange(User*, Channel *c, const std::string&)
	{
		if (c->IsModeSet('P'))
			dirty = true;
	}

	void OnBackgroundTimer(time_t)
	{
		if (dirty)
			WriteDatabase(this, save_listmodes);
		dirty = false;
	}

	void Prioritize()
	{
		// XXX: Load the DB here because the order in which modules are init()ed at boot is
		// alphabetical, this means we must wait until all modules have done their init()
		// to be able to set the modes they provide (e.g.: m_stripcolor is inited after us)
		// Prioritize() is called after all module initialization is complete, consequently
		// all modes are available now
		if (loaded)
			return;

		loaded = true;

		// Load only when there are no linked servers - we set the TS of the channels we
		// create to the current time, this can lead to desync because spanningtree has
		// no way of knowing what we do
		ProtoServerList serverlist;
		ServerInstance->PI->GetServerList(serverlist);
		if (serverlist.size() < 2)
		{
			try
			{
				LoadDatabase();
			}
			catch (CoreException& e)
			{
				ServerInstance->Logs->Log("m_permchannels", DEFAULT, "Error loading permchannels database: " + std::string(e.GetReason()));
			}
		}
	}

	void ProtoSendMode(void* opaque, TargetTypeFlags type, void* target, const std::vector<std::string>& modes, const std::vector<TranslateType>& translate)
	{
		// We never pass an empty modelist but better be sure
		if (modes.empty())
			return;

		ListModeData* lm = static_cast<ListModeData*>(opaque);

		// Append the mode letters without the trailing '+' (for example "IIII", "gg")
		lm->modes.append(modes[0].begin()+1, modes[0].end());

		// Append the parameters
		for (std::vector<std::string>::const_iterator i = modes.begin()+1; i != modes.end(); ++i)
		{
			lm->params += *i;
			lm->params += ' ';
		}
	}

	virtual Version GetVersion()
	{
		return Version("Provides support for channel mode +P to provide permanent channels",VF_VENDOR);
	}

	virtual ModResult OnChannelPreDelete(Channel *c)
	{
		if (c->IsModeSet('P'))
			return MOD_RES_DENY;

		return MOD_RES_PASSTHRU;
	}
};

MODULE_INIT(ModulePermanentChannels)
