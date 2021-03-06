/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2013-2014 Attila Molnar <attilamolnar@hush.com>
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


#pragma once

#include <iterator>

struct intrusive_list_def_tag { };

template <typename T, typename Tag = intrusive_list_def_tag> class intrusive_list;

template <typename T, typename Tag = intrusive_list_def_tag>
class intrusive_list_node
{
	T* ptr_next;
	T* ptr_prev;

	void unlink()
	{
		if (ptr_next)
			ptr_next->intrusive_list_node<T, Tag>::ptr_prev = this->ptr_prev;
		if (ptr_prev)
			ptr_prev->intrusive_list_node<T, Tag>::ptr_next = this->ptr_next;
		ptr_next = ptr_prev = NULL;
	}

 public:
	intrusive_list_node()
		: ptr_next(NULL)
		, ptr_prev(NULL)
	{
	}

	friend class intrusive_list<T, Tag>;
};

template <typename T, typename Tag>
class intrusive_list
{
 public:
	class iterator : public std::iterator<std::bidirectional_iterator_tag, T*>
	{
		T* curr;

	 public:
		iterator(T* i = NULL)
			: curr(i)
		{
		}

		iterator& operator++()
		{
			curr = curr->intrusive_list_node<T, Tag>::ptr_next;
			return *this;
		}

		iterator operator++(int)
		{
			iterator ret(*this);
			operator++();
			return ret;
		}

		iterator& operator--()
		{
			curr = curr->intrusive_list_node<T, Tag>::ptr_prev;
			return *this;
		}

		iterator operator--(int)
		{
			iterator ret(*this);
			operator--();
			return ret;
		}

		bool operator==(const iterator& other) const { return (curr == other.curr); }
		bool operator!=(const iterator& other) const { return (curr != other.curr); }
		T* operator*() const { return curr; }
	};

	typedef iterator const_iterator;

	intrusive_list()
		: listhead(NULL)
		, listsize(0)
	{
	}

	bool empty() const
	{
		return (size() == 0);
	}

	size_t size() const
	{
		return listsize;
	}

	iterator begin() const
	{
		return iterator(listhead);
	}

	iterator end() const
	{
		return iterator();
	}

	void pop_front()
	{
		erase(listhead);
	}

	T* front() const
	{
		return listhead;
	}

	void push_front(T* x)
	{
		if (listsize++)
		{
			x->intrusive_list_node<T, Tag>::ptr_next = listhead;
			listhead->intrusive_list_node<T, Tag>::ptr_prev = x;
		}
		listhead = x;
	}

	void erase(const iterator& it)
	{
		erase(*it);
	}

	void erase(T* x)
	{
		if (listhead == x)
			listhead = x->intrusive_list_node<T, Tag>::ptr_next;
		x->intrusive_list_node<T, Tag>::unlink();
		listsize--;
	}

 private:
	T* listhead;
	size_t listsize;
};
