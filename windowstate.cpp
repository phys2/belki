#include "windowstate.h"

WindowState::WindowState()
{
	auto addOrderItem = [this] (QString name, QString icon, int id) {
		auto item = new QStandardItem(name);
		if (!icon.isEmpty())
			item->setIcon(QIcon(icon));
		item->setData(id, Qt::UserRole);
		orderModel.appendRow(item);
	};

	/* prepare default structure items */
	addOrderItem("Position in file", {}, Order::FILE);
	addOrderItem("Protein name", {}, Order::NAME);
	addOrderItem("Hierarchy", {}, Order::HIERARCHY);
	addOrderItem("Clustering/Annotations", {}, Order::CLUSTERING);
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
