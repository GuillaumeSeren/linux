// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2019 NXP */
#include <linux/fsl/enetc_mdio.h>
#include <linux/of_mdio.h>
#include "enetc_pf.h"

#define NETC_EMDIO_VEN_ID	0x1131
#define NETC_EMDIO_DEV_ID	0xee00
#define ENETC_MDIO_DEV_ID	0xee01
#define ENETC_MDIO_DEV_NAME	"FSL PCIe IE Central MDIO"
#define ENETC_MDIO_BUS_NAME	ENETC_MDIO_DEV_NAME " Bus"
#define ENETC_MDIO_DRV_NAME	ENETC_MDIO_DEV_NAME " driver"

DEFINE_STATIC_KEY_FALSE(enetc_has_err050089);
EXPORT_SYMBOL_GPL(enetc_has_err050089);

static void enetc_emdio_enable_err050089(struct pci_dev *pdev)
{
	if (pdev->vendor == PCI_VENDOR_ID_FREESCALE &&
	    pdev->device == ENETC_MDIO_DEV_ID) {
		static_branch_inc(&enetc_has_err050089);
		dev_info(&pdev->dev, "Enabled ERR050089 workaround\n");
	}
}

static void enetc_emdio_disable_err050089(struct pci_dev *pdev)
{
	if (pdev->vendor == PCI_VENDOR_ID_FREESCALE &&
	    pdev->device == ENETC_MDIO_DEV_ID) {
		static_branch_dec(&enetc_has_err050089);
		if (!static_key_enabled(&enetc_has_err050089.key))
			dev_info(&pdev->dev, "Disabled ERR050089 workaround\n");
	}
}

static int enetc_pci_mdio_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct enetc_mdio_priv *mdio_priv;
	struct device *dev = &pdev->dev;
	void __iomem *port_regs;
	struct enetc_hw *hw;
	struct mii_bus *bus;
	int err;

	port_regs = pci_iomap(pdev, 0, 0);
	if (!port_regs) {
		dev_err(dev, "iomap failed\n");
		err = -ENXIO;
		goto err_ioremap;
	}

	hw = enetc_hw_alloc(dev, port_regs);
	if (IS_ERR(hw)) {
		err = PTR_ERR(hw);
		goto err_hw_alloc;
	}

	bus = devm_mdiobus_alloc_size(dev, sizeof(*mdio_priv));
	if (!bus) {
		err = -ENOMEM;
		goto err_mdiobus_alloc;
	}

	bus->name = ENETC_MDIO_BUS_NAME;
	bus->read = enetc_mdio_read_c22;
	bus->write = enetc_mdio_write_c22;
	bus->read_c45 = enetc_mdio_read_c45;
	bus->write_c45 = enetc_mdio_write_c45;
	bus->parent = dev;
	mdio_priv = bus->priv;
	mdio_priv->hw = hw;
	mdio_priv->mdio_base = ENETC_EMDIO_BASE;
	snprintf(bus->id, MII_BUS_ID_SIZE, "%s", dev_name(dev));

	pcie_flr(pdev);
	err = pci_enable_device_mem(pdev);
	if (err) {
		dev_err(dev, "device enable failed\n");
		goto err_pci_enable;
	}

	err = pci_request_region(pdev, 0, KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "pci_request_region failed\n");
		goto err_pci_mem_reg;
	}

	enetc_emdio_enable_err050089(pdev);

	err = of_mdiobus_register(bus, dev->of_node);
	if (err)
		goto err_mdiobus_reg;

	pci_set_drvdata(pdev, bus);

	return 0;

err_mdiobus_reg:
	enetc_emdio_disable_err050089(pdev);
	pci_release_region(pdev, 0);
err_pci_mem_reg:
	pci_disable_device(pdev);
err_pci_enable:
err_mdiobus_alloc:
err_hw_alloc:
	iounmap(port_regs);
err_ioremap:
	return err;
}

static void enetc_pci_mdio_remove(struct pci_dev *pdev)
{
	struct mii_bus *bus = pci_get_drvdata(pdev);
	struct enetc_mdio_priv *mdio_priv;

	mdiobus_unregister(bus);

	enetc_emdio_disable_err050089(pdev);

	mdio_priv = bus->priv;
	iounmap(mdio_priv->hw->port);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
}

static const struct pci_device_id enetc_pci_mdio_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_FREESCALE, ENETC_MDIO_DEV_ID) },
	{ PCI_DEVICE(NETC_EMDIO_VEN_ID, NETC_EMDIO_DEV_ID) },
	{ 0, } /* End of table. */
};
MODULE_DEVICE_TABLE(pci, enetc_pci_mdio_id_table);

static struct pci_driver enetc_pci_mdio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = enetc_pci_mdio_id_table,
	.probe = enetc_pci_mdio_probe,
	.remove = enetc_pci_mdio_remove,
};
module_pci_driver(enetc_pci_mdio_driver);

MODULE_DESCRIPTION(ENETC_MDIO_DRV_NAME);
MODULE_LICENSE("Dual BSD/GPL");
