#include "windowstate.h"

WindowState::WindowState()
{
	auto addOrderItem = [this] (QString name, QIcon icon, int id) {
		auto item = new QStandardItem(name);
		if (!icon.isNull())
			item->setIcon(QIcon(icon));
		item->setData(id, Qt::UserRole);
		orderModel.appendRow(item);
	};

	/* prepare order items */
	addOrderItem("Position in file", QIcon::fromTheme("sort_incr"), Order::FILE);
	addOrderItem("Protein name", QIcon::fromTheme("sort-name"), Order::NAME);
	addOrderItem("Hierarchy", QIcon{":/icons/type-hierarchy.svg"}, Order::HIERARCHY);
	addOrderItem("Clustering/Annotations", QIcon{":/icons/type-annotations.svg"}, Order::CLUSTERING);
}

void WindowState::setOrder(Order::Type type)
{
	if (type == preferredOrder)
		return; // we are done

	preferredOrder = type;
	// translate type to description
	if (type == Order::FILE || type == Order::NAME)
		order = {type};
	if (type == Order::HIERARCHY)
		order = {type, hierarchy};
	if (type == Order::CLUSTERING) {
		if (annotations.type == Annotations::Meta::HIERCUT)
			order = {Order::HIERARCHY, hierarchy};
		else
			order = {type, annotations};
	}

	emit orderChanged();
}
